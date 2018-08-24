#define _XOPEN_SOURCE 500
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include "stringop.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct sway_container *workspace_get_initial_output(const char *name) {
	struct sway_container *parent;
	// Search for workspace<->output pair
	int e = config->workspace_outputs->length;
	for (int i = 0; i < config->workspace_outputs->length; ++i) {
		struct workspace_output *wso = config->workspace_outputs->items[i];
		if (strcasecmp(wso->workspace, name) == 0) {
			// Find output to use if it exists
			e = root_container.children->length;
			for (i = 0; i < e; ++i) {
				parent = root_container.children->items[i];
				if (strcmp(parent->name, wso->output) == 0) {
					return parent;
				}
			}
			break;
		}
	}
	// Otherwise put it on the focused output
	struct sway_seat *seat = input_manager_current_seat(input_manager);
	struct sway_container *focus =
		seat_get_focus_inactive(seat, &root_container);
	parent = focus;
	parent = container_parent(parent, C_OUTPUT);
	return parent;
}

struct sway_container *workspace_create(struct sway_container *output,
		const char *name) {
	if (output == NULL) {
		output = workspace_get_initial_output(name);
	}

	wlr_log(WLR_DEBUG, "Added workspace %s for output %s", name, output->name);
	struct sway_container *workspace = container_create(C_WORKSPACE);

	workspace->x = output->x;
	workspace->y = output->y;
	workspace->width = output->width;
	workspace->height = output->height;
	workspace->name = !name ? NULL : strdup(name);
	workspace->prev_split_layout = L_NONE;
	workspace->layout = container_get_default_layout(output);

	struct sway_workspace *swayws = calloc(1, sizeof(struct sway_workspace));
	if (!swayws) {
		return NULL;
	}
	swayws->swayc = workspace;
	swayws->floating = create_list();
	swayws->output_priority = create_list();
	workspace->sway_workspace = swayws;
	workspace_output_add_priority(workspace, output);

	container_add_child(output, workspace);
	output_sort_workspaces(output);
	container_create_notify(workspace);

	return workspace;
}

void workspace_destroy(struct sway_container *workspace) {
	if (!sway_assert(workspace->type == C_WORKSPACE, "Expected a workspace")) {
		return;
	}
	if (!sway_assert(workspace->destroying,
				"Tried to free workspace which wasn't marked as destroying")) {
		return;
	}
	if (!sway_assert(workspace->ntxnrefs == 0, "Tried to free workspace "
				"which is still referenced by transactions")) {
		return;
	}
	// sway_workspace
	struct sway_workspace *ws = workspace->sway_workspace;
	list_foreach(ws->output_priority, free);
	list_free(ws->output_priority);
	list_free(ws->floating);
	free(ws);

	// swayc
	free(workspace->name);
	free(workspace->formatted_title);
	wlr_texture_destroy(workspace->title_focused);
	wlr_texture_destroy(workspace->title_focused_inactive);
	wlr_texture_destroy(workspace->title_unfocused);
	wlr_texture_destroy(workspace->title_urgent);
	list_free(workspace->children);
	list_free(workspace->current.children);
	list_free(workspace->outputs);
	free(workspace);
}

void workspace_begin_destroy(struct sway_container *workspace) {
	if (!sway_assert(workspace->type == C_WORKSPACE, "Expected a workspace")) {
		return;
	}
	wlr_log(WLR_DEBUG, "Destroying workspace '%s'", workspace->name);
	wl_signal_emit(&workspace->events.destroy, workspace);
	ipc_event_workspace(NULL, workspace, "empty"); // intentional

	workspace->destroying = true;
	container_set_dirty(workspace);

	if (workspace->parent) {
		container_remove_child(workspace);
	}
}

void workspace_consider_destroy(struct sway_container *ws) {
	if (!sway_assert(ws->type == C_WORKSPACE, "Expected a workspace")) {
		return;
	}
	struct sway_seat *seat = input_manager_current_seat(input_manager);
	if (ws->children->length == 0 && ws->sway_workspace->floating->length == 0
			&& seat_get_active_child(seat, ws->parent) != ws) {
		workspace_begin_destroy(ws);
	}
}

char *prev_workspace_name = NULL;

void next_name_map(struct sway_container *ws, void *data) {
	int *count = data;
	++count;
}

static bool workspace_valid_on_output(const char *output_name,
		const char *ws_name) {
	int i;
	for (i = 0; i < config->workspace_outputs->length; ++i) {
		struct workspace_output *wso = config->workspace_outputs->items[i];
		if (strcasecmp(wso->workspace, ws_name) == 0) {
			if (strcasecmp(wso->output, output_name) != 0) {
				return false;
			}
		}
	}

	return true;
}

static void workspace_name_from_binding(const struct sway_binding * binding,
		const char* output_name, int *min_order, char **earliest_name) {
	char *cmdlist = strdup(binding->command_str);
	char *dup = cmdlist;
	char *name = NULL;

	// workspace n
	char *cmd = argsep(&cmdlist, " ");
	if (cmdlist) {
		name = argsep(&cmdlist, ",;");
	}

	// TODO: support "move container to workspace" bindings as well

	if (strcmp("workspace", cmd) == 0 && name) {
		char *_target = strdup(name);
		_target = do_var_replacement(_target);
		strip_quotes(_target);
		wlr_log(WLR_DEBUG, "Got valid workspace command for target: '%s'",
				_target);

		// Make sure that the command references an actual workspace
		// not a command about workspaces
		if (strcmp(_target, "next") == 0 ||
				strcmp(_target, "prev") == 0 ||
				strcmp(_target, "next_on_output") == 0 ||
				strcmp(_target, "prev_on_output") == 0 ||
				strcmp(_target, "number") == 0 ||
				strcmp(_target, "back_and_forth") == 0 ||
				strcmp(_target, "current") == 0) {
			free(_target);
			free(dup);
			return;
		}

		// If the command is workspace number <name>, isolate the name
		if (strncmp(_target, "number ", strlen("number ")) == 0) {
			size_t length = strlen(_target) - strlen("number ") + 1;
			char *temp = malloc(length);
			strncpy(temp, _target + strlen("number "), length - 1);
			temp[length - 1] = '\0';
			free(_target);
			_target = temp;
			wlr_log(WLR_DEBUG, "Isolated name from workspace number: '%s'", _target);

			// Make sure the workspace number doesn't already exist
			if (isdigit(_target[0]) && workspace_by_number(_target)) {
				free(_target);
				free(dup);
				return;
			}
		}

		// Make sure that the workspace doesn't already exist
		if (workspace_by_name(_target)) {
			free(_target);
			free(dup);
			return;
		}

		// make sure that the workspace can appear on the given
		// output
		if (!workspace_valid_on_output(output_name, _target)) {
			free(_target);
			free(dup);
			return;
		}

		if (binding->order < *min_order) {
			*min_order = binding->order;
			free(*earliest_name);
			*earliest_name = _target;
			wlr_log(WLR_DEBUG, "Workspace: Found free name %s", _target);
		} else {
			free(_target);
		}
	}
	free(dup);
}

char *workspace_next_name(const char *output_name) {
	wlr_log(WLR_DEBUG, "Workspace: Generating new workspace name for output %s",
			output_name);
	// Scan for available workspace names by looking through output-workspace
	// assignments primarily, falling back to bindings and numbers.
	struct sway_mode *mode = config->current_mode;

	int order = INT_MAX;
	char *target = NULL;
	for (int i = 0; i < mode->keysym_bindings->length; ++i) {
		workspace_name_from_binding(mode->keysym_bindings->items[i],
				output_name, &order, &target);
	}
	for (int i = 0; i < mode->keycode_bindings->length; ++i) {
		workspace_name_from_binding(mode->keycode_bindings->items[i],
				output_name, &order, &target);
	}
	for (int i = 0; i < config->workspace_outputs->length; ++i) {
		// Unlike with bindings, this does not guarantee order
		const struct workspace_output *wso = config->workspace_outputs->items[i];
		if (strcmp(wso->output, output_name) == 0
				&& workspace_by_name(wso->workspace) == NULL) {
			free(target);
			target = strdup(wso->workspace);
			break;
		}
	}
	if (target != NULL) {
		return target;
	}
	// As a fall back, get the current number of active workspaces
	// and return that + 1 for the next workspace's name
	int ws_num = root_container.children->length;
	int l = snprintf(NULL, 0, "%d", ws_num);
	char *name = malloc(l + 1);
	if (!sway_assert(name, "Cloud not allocate workspace name")) {
		return NULL;
	}
	sprintf(name, "%d", ws_num++);
	return name;
}

static bool _workspace_by_number(struct sway_container *view, void *data) {
	if (view->type != C_WORKSPACE) {
		return false;
	}
	char *name = data;
	char *view_name = view->name;
	while (isdigit(*name)) {
		if (*name++ != *view_name++) {
			return false;
		}
	}
	return !isdigit(*view_name);
}

struct sway_container *workspace_by_number(const char* name) {
	return root_find_workspace(_workspace_by_number, (void *) name);
}

static bool _workspace_by_name(struct sway_container *view, void *data) {
	return (view->type == C_WORKSPACE) &&
		   (strcasecmp(view->name, (char *) data) == 0);
}

struct sway_container *workspace_by_name(const char *name) {
	struct sway_seat *seat = input_manager_current_seat(input_manager);
	struct sway_container *current_workspace = NULL, *current_output = NULL;
	struct sway_container *focus = seat_get_focus(seat);
	if (focus) {
		current_workspace = focus->type == C_WORKSPACE ?
			focus : container_parent(focus, C_WORKSPACE);
		current_output = container_parent(focus, C_OUTPUT);
	}

	if (strcmp(name, "prev") == 0) {
		return workspace_prev(current_workspace);
	} else if (strcmp(name, "prev_on_output") == 0) {
		return workspace_output_prev(current_output);
	} else if (strcmp(name, "next") == 0) {
		return workspace_next(current_workspace);
	} else if (strcmp(name, "next_on_output") == 0) {
		return workspace_output_next(current_output);
	} else if (strcmp(name, "current") == 0) {
		return current_workspace;
	} else if (strcasecmp(name, "back_and_forth") == 0) {
		return prev_workspace_name ?
			root_find_workspace(_workspace_by_name, (void*)prev_workspace_name)
			: NULL;
	} else {
		return root_find_workspace(_workspace_by_name, (void*)name);
	}
}

/**
 * Get the previous or next workspace on the specified output. Wraps around at
 * the end and beginning.  If next is false, the previous workspace is returned,
 * otherwise the next one is returned.
 */
static struct sway_container *workspace_output_prev_next_impl(
		struct sway_container *output, int dir) {
	if (!output) {
		return NULL;
	}
	if (!sway_assert(output->type == C_OUTPUT,
				"Argument must be an output, is %d", output->type)) {
		return NULL;
	}

	struct sway_seat *seat = input_manager_current_seat(input_manager);
	struct sway_container *focus = seat_get_focus_inactive(seat, output);
	struct sway_container *workspace = (focus->type == C_WORKSPACE ?
			focus :
			container_parent(focus, C_WORKSPACE));

	int index = list_find(output->children, workspace);
	size_t new_index = wrap(index + dir, output->children->length);
	return output->children->items[new_index];
}

/**
 * Get the previous or next workspace. If the first/last workspace on an output
 * is active, proceed to the previous/next output's previous/next workspace.
 */
static struct sway_container *workspace_prev_next_impl(
		struct sway_container *workspace, int dir) {
	if (!workspace) {
		return NULL;
	}
	if (!sway_assert(workspace->type == C_WORKSPACE,
				"Argument must be a workspace, is %d", workspace->type)) {
		return NULL;
	}

	struct sway_container *output = workspace->parent;
	int index = list_find(output->children, workspace);
	int new_index = index + dir;

	if (new_index >= 0 && new_index < output->children->length) {
		return output->children->items[index + dir];
	}

	// Look on a different output
	int output_index = list_find(root_container.children, output);
	new_index = wrap(output_index + dir, root_container.children->length);
	output = root_container.children->items[new_index];

	if (dir == 1) {
		return output->children->items[0];
	} else {
		return output->children->items[output->children->length - 1];
	}
}

struct sway_container *workspace_output_next(struct sway_container *current) {
	return workspace_output_prev_next_impl(current, 1);
}

struct sway_container *workspace_next(struct sway_container *current) {
	return workspace_prev_next_impl(current, 1);
}

struct sway_container *workspace_output_prev(struct sway_container *current) {
	return workspace_output_prev_next_impl(current, -1);
}

struct sway_container *workspace_prev(struct sway_container *current) {
	return workspace_prev_next_impl(current, -1);
}

bool workspace_switch(struct sway_container *workspace,
		bool no_auto_back_and_forth) {
	if (!workspace) {
		return false;
	}
	struct sway_seat *seat = input_manager_current_seat(input_manager);
	struct sway_container *focus =
		seat_get_focus_inactive(seat, &root_container);
	if (!seat || !focus) {
		return false;
	}
	struct sway_container *active_ws = focus;
	if (active_ws->type != C_WORKSPACE) {
		active_ws = container_parent(focus, C_WORKSPACE);
	}

	if (!no_auto_back_and_forth && config->auto_back_and_forth
			&& active_ws == workspace
			&& prev_workspace_name) {
		struct sway_container *new_ws = workspace_by_name(prev_workspace_name);
		workspace = new_ws ?
			new_ws :
			workspace_create(NULL, prev_workspace_name);
	}

	if (!prev_workspace_name || (strcmp(prev_workspace_name, active_ws->name)
				&& active_ws != workspace)) {
		free(prev_workspace_name);
		prev_workspace_name = malloc(strlen(active_ws->name) + 1);
		if (!prev_workspace_name) {
			wlr_log(WLR_ERROR, "Unable to allocate previous workspace name");
			return false;
		}
		strcpy(prev_workspace_name, active_ws->name);
	}

	// Move sticky containers to new workspace
	struct sway_container *next_output = workspace->parent;
	struct sway_container *next_output_prev_ws =
		seat_get_active_child(seat, next_output);
	list_t *floating = next_output_prev_ws->sway_workspace->floating;
	bool has_sticky = false;
	if (workspace != next_output_prev_ws) {
		for (int i = 0; i < floating->length; ++i) {
			struct sway_container *floater = floating->items[i];
			if (floater->is_sticky) {
				has_sticky = true;
				container_remove_child(floater);
				workspace_add_floating(workspace, floater);
				if (floater == focus) {
					seat_set_focus(seat, NULL);
					seat_set_focus(seat, floater);
				}
				--i;
			}
		}
	}

	wlr_log(WLR_DEBUG, "Switching to workspace %p:%s",
		workspace, workspace->name);
	struct sway_container *next = seat_get_focus_inactive(seat, workspace);
	if (next == NULL) {
		next = workspace;
	}
	if (has_sticky) {
		// If there's a sticky container, we might be setting focus to the same
		// container that's already focused, so seat_set_focus is effectively a
		// no op. We therefore need to send the IPC event and clean up the old
		// workspace here.
		ipc_event_workspace(active_ws, workspace, "focus");
		workspace_consider_destroy(active_ws);
	}
	seat_set_focus(seat, next);
	struct sway_container *output = container_parent(workspace, C_OUTPUT);
	arrange_windows(output);
	return true;
}

bool workspace_is_visible(struct sway_container *ws) {
	if (ws->destroying) {
		return false;
	}
	struct sway_container *output = container_parent(ws, C_OUTPUT);
	struct sway_seat *seat = input_manager_current_seat(input_manager);
	struct sway_container *focus = seat_get_focus_inactive(seat, output);
	if (focus->type != C_WORKSPACE) {
		focus = container_parent(focus, C_WORKSPACE);
	}
	return focus == ws;
}

bool workspace_is_empty(struct sway_container *ws) {
	if (!sway_assert(ws->type == C_WORKSPACE, "Expected a workspace")) {
		return false;
	}
	if (ws->children->length) {
		return false;
	}
	// Sticky views are not considered to be part of this workspace
	list_t *floating = ws->sway_workspace->floating;
	for (int i = 0; i < floating->length; ++i) {
		struct sway_container *floater = floating->items[i];
		if (!floater->is_sticky) {
			return false;
		}
	}
	return true;
}

static int find_output(const void *id1, const void *id2) {
	return strcmp(id1, id2) ? 0 : 1;
}

void workspace_output_raise_priority(struct sway_container *workspace,
		struct sway_container *old_output, struct sway_container *output) {
	struct sway_workspace *ws = workspace->sway_workspace;

	int old_index = list_seq_find(ws->output_priority, find_output,
			old_output->name);
	if (old_index < 0) {
		return;
	}

	int new_index = list_seq_find(ws->output_priority, find_output,
			output->name);
	if (new_index < 0) {
		list_insert(ws->output_priority, old_index, strdup(output->name));
	} else if (new_index > old_index) {
		char *name = ws->output_priority->items[new_index];
		list_del(ws->output_priority, new_index);
		list_insert(ws->output_priority, old_index, name);
	}
}

void workspace_output_add_priority(struct sway_container *workspace,
		struct sway_container *output) {
	int index = list_seq_find(workspace->sway_workspace->output_priority,
			find_output, output->name);
	if (index < 0) {
		list_add(workspace->sway_workspace->output_priority,
				strdup(output->name));
	}
}

static bool _output_by_name(struct sway_container *output, void *data) {
	return output->type == C_OUTPUT && strcasecmp(output->name, data) == 0;
}

struct sway_container *workspace_output_get_highest_available(
		struct sway_container *ws, struct sway_container *exclude) {
	for (int i = 0; i < ws->sway_workspace->output_priority->length; i++) {
		char *name = ws->sway_workspace->output_priority->items[i];
		if (exclude && strcasecmp(name, exclude->name) == 0) {
			continue;
		}

		struct sway_container *output = root_find_output(_output_by_name, name);
		if (output) {
			return output;
		}
	}

	return NULL;
}

static bool find_urgent_iterator(struct sway_container *con, void *data) {
	return con->type == C_VIEW && view_is_urgent(con->sway_view);
}

void workspace_detect_urgent(struct sway_container *workspace) {
	bool new_urgent = (bool)workspace_find_container(workspace,
			find_urgent_iterator, NULL);

	if (workspace->sway_workspace->urgent != new_urgent) {
		workspace->sway_workspace->urgent = new_urgent;
		ipc_event_workspace(NULL, workspace, "urgent");
		container_damage_whole(workspace);
	}
}

void workspace_for_each_container(struct sway_container *ws,
		void (*f)(struct sway_container *con, void *data), void *data) {
	if (!sway_assert(ws->type == C_WORKSPACE, "Expected a workspace")) {
		return;
	}
	// Tiling
	for (int i = 0; i < ws->children->length; ++i) {
		struct sway_container *container = ws->children->items[i];
		f(container, data);
		container_for_each_child(container, f, data);
	}
	// Floating
	for (int i = 0; i < ws->sway_workspace->floating->length; ++i) {
		struct sway_container *container =
			ws->sway_workspace->floating->items[i];
		f(container, data);
		container_for_each_child(container, f, data);
	}
}

struct sway_container *workspace_find_container(struct sway_container *ws,
		bool (*test)(struct sway_container *con, void *data), void *data) {
	if (!sway_assert(ws->type == C_WORKSPACE, "Expected a workspace")) {
		return NULL;
	}
	struct sway_container *result = NULL;
	// Tiling
	for (int i = 0; i < ws->children->length; ++i) {
		struct sway_container *child = ws->children->items[i];
		if (test(child, data)) {
			return child;
		}
		if ((result = container_find_child(child, test, data))) {
			return result;
		}
	}
	// Floating
	for (int i = 0; i < ws->sway_workspace->floating->length; ++i) {
		struct sway_container *child = ws->sway_workspace->floating->items[i];
		if (test(child, data)) {
			return child;
		}
		if ((result = container_find_child(child, test, data))) {
			return result;
		}
	}
	return NULL;
}

struct sway_container *workspace_wrap_children(struct sway_container *ws) {
	struct sway_container *middle = container_create(C_CONTAINER);
	middle->layout = ws->layout;
	while (ws->children->length) {
		struct sway_container *child = ws->children->items[0];
		container_remove_child(child);
		container_add_child(middle, child);
	}
	container_add_child(ws, middle);
	return middle;
}

void workspace_add_floating(struct sway_container *workspace,
		struct sway_container *con) {
	if (!sway_assert(workspace->type == C_WORKSPACE, "Expected a workspace")) {
		return;
	}
	if (!sway_assert(con->parent == NULL, "Expected an orphan container")) {
		return;
	}

	list_add(workspace->sway_workspace->floating, con);
	con->parent = workspace;
	container_set_dirty(workspace);
	container_set_dirty(con);
}
