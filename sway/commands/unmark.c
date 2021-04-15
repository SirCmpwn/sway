#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

static void remove_all_marks_iterator(struct sway_container *con, void *data) {
	container_clear_marks(con);
	container_update_marks_textures(con);
}

// unmark                  Remove all marks from all views
// unmark foo              Remove single mark from whichever view has it
// [criteria] unmark       Remove all marks from matched view
// [criteria] unmark foo   Remove single mark from matched view

struct cmd_results *cmd_unmark(int argc, char **argv) {
	// Determine the container
	struct sway_container *con = NULL;
	if (config->handler_context.node_overridden) {
		con = config->handler_context.container;
	}

	// Determine the mark
	char *mark = NULL;
	if (argc > 0) {
		mark = join_args(argv, argc);
	}

	if (con && mark) {
		// Remove the mark from the given container
		if (container_has_mark(con, mark)) {
			container_find_and_unmark(mark);
		}
	} else if (con && !mark) {
		// Clear all marks from the given container
		container_clear_marks(con);
		container_update_marks_textures(con);
	} else if (!con && mark) {
		// Remove mark from whichever container has it
		container_find_and_unmark(mark);
	} else {
		// Remove all marks from all containers
		root_for_each_container(remove_all_marks_iterator, NULL);
	}
	free(mark);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
