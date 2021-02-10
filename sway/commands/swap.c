#define _POSIX_C_SOURCE 200809L
#include <strings.h>
#include "config.h"
#include "log.h"
#include "sway/commands.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "stringop.h"

static const char expected_syntax[] =
	"Expected 'swap container with id|con_id|mark <arg>'";

static bool test_con_id(struct sway_container *container, void *data) {
	size_t *con_id = data;
	return container->node.id == *con_id;
}

#if HAVE_XWAYLAND
static bool test_id(struct sway_container *container, void *data) {
	xcb_window_t *wid = data;
	return (container->view && container->view->type == SWAY_VIEW_XWAYLAND
			&& container->view->wlr_xwayland_surface->window_id == *wid);
}
#endif

static bool test_mark(struct sway_container *container, void *mark) {
	if (container->marks->length) {
		return list_seq_find(container->marks,
				(int (*)(const void *, const void *))strcmp, mark) != -1;
	}
	return false;
}

struct cmd_results *cmd_swap(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "swap", EXPECTED_AT_LEAST, 4))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}

	if (strcasecmp(argv[0], "container") || strcasecmp(argv[1], "with")) {
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	struct sway_container *current = config->handler_context.container;
	struct sway_container *other = NULL;

	char *value = join_args(argv + 3, argc - 3);
	if (strcasecmp(argv[2], "id") == 0) {
#if HAVE_XWAYLAND
		xcb_window_t id = strtol(value, NULL, 0);
		other = root_find_container(test_id, &id);
#endif
	} else if (strcasecmp(argv[2], "con_id") == 0) {
		size_t con_id = atoi(value);
		other = root_find_container(test_con_id, &con_id);
	} else if (strcasecmp(argv[2], "mark") == 0) {
		other = root_find_container(test_mark, value);
	} else {
		free(value);
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	if (!other) {
		error = cmd_results_new(CMD_FAILURE,
				"Failed to find %s '%s'", argv[2], value);
	} else if (!current) {
		error = cmd_results_new(CMD_FAILURE,
				"Can only swap with containers and views");
	} else if (container_has_ancestor(current, other)
			|| container_has_ancestor(other, current)) {
		error = cmd_results_new(CMD_FAILURE,
				"Cannot swap ancestor and descendant");
	}

	free(value);

	if (error) {
		return error;
	}

	container_swap(current, other);

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		struct sway_node *current_parent = node_get_parent(&current->node);
		struct sway_node *other_parent = node_get_parent(&other->node);
		if (current_parent) {
			arrange_node(current_parent);
		}
		if (other_parent && current_parent != other_parent) {
			arrange_node(other_parent);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
