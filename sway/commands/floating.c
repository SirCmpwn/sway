#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "list.h"
#include "util.h"

struct cmd_results *cmd_floating(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "floating", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *container = config->handler_context.container;
	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!container && workspace->tiling->length == 0) {
		return cmd_results_new(CMD_INVALID, "Can't float an empty workspace");
	}
	if (!container) {
		// Wrap the workspace's children in a container so we can float it
		container = workspace_wrap_children(workspace);
		workspace->layout = L_HORIZ;
		seat_set_focus_container(config->handler_context.seat, container);
	}

	if (container_is_scratchpad_hidden(container)) {
		return cmd_results_new(CMD_INVALID,
				"Can't change floating on hidden scratchpad container");
	}

	// If the container is in a floating split container,
	// operate on the split container instead of the child.
	if (container_is_floating_or_child(container)) {
		while (container->pending.parent) {
			container = container->pending.parent;
		}
	}

	bool wants_floating =
		parse_boolean(argv[0], container_is_floating(container));

	container_set_floating(container, wants_floating);

	// Floating containers in the scratchpad should be ignored
	if (container->pending.workspace) {
		arrange_workspace(container->pending.workspace);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
