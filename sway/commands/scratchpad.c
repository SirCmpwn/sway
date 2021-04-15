#include "log.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"

static void scratchpad_toggle_auto(void) {
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_container *focus = seat_get_focused_container(seat);
	struct sway_workspace *ws = seat_get_focused_workspace(seat);
	if (!ws) {
		sway_log(SWAY_DEBUG,
				"No focused workspace to toggle scratchpad windows on");
		return;
	}

	// If the focus is in a floating split container,
	// operate on the split container instead of the child.
	if (focus && container_is_floating_or_child(focus)) {
		while (focus->pending.parent) {
			focus = focus->pending.parent;
		}
	}

	// Check if the currently focused window is a scratchpad window and should
	// be hidden again.
	if (focus && focus->scratchpad) {
		sway_log(SWAY_DEBUG, "Focus is a scratchpad window - hiding %s",
				focus->title);
		root_scratchpad_hide(focus);
		return;
	}

	// Check if there is an unfocused scratchpad window on the current workspace
	// and focus it.
	for (int i = 0; i < ws->floating->length; ++i) {
		struct sway_container *floater = ws->floating->items[i];
		if (floater->scratchpad && focus != floater) {
			sway_log(SWAY_DEBUG,
					"Focusing other scratchpad window (%s) in this workspace",
					floater->title);
			root_scratchpad_show(floater);
			return;
		}
	}

	// Check if there is a visible scratchpad window on another workspace.
	// In this case we move it to the current workspace.
	for (int i = 0; i < root->scratchpad->length; ++i) {
		struct sway_container *con = root->scratchpad->items[i];
		if (con->pending.parent) {
			sway_log(SWAY_DEBUG,
					"Moving a visible scratchpad window (%s) to this workspace",
					con->title);
			root_scratchpad_show(con);
			ipc_event_window(con, "move");
			return;
		}
	}

	// Take the container at the bottom of the scratchpad list
	if (!sway_assert(root->scratchpad->length, "Scratchpad is empty")) {
		return;
	}
	struct sway_container *con = root->scratchpad->items[0];
	sway_log(SWAY_DEBUG, "Showing %s from list", con->title);
	root_scratchpad_show(con);
	ipc_event_window(con, "move");
}

static void scratchpad_toggle_container(struct sway_container *con) {
	if (!sway_assert(con->scratchpad, "Container isn't in the scratchpad")) {
		return;
	}

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_workspace *ws = seat_get_focused_workspace(seat);
	// Check if it matches a currently visible scratchpad window and hide it.
	if (con->pending.workspace && ws == con->pending.workspace) {
		root_scratchpad_hide(con);
		return;
	}

	root_scratchpad_show(con);
	ipc_event_window(con, "move");
}

struct cmd_results *cmd_scratchpad(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "scratchpad", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	if (strcmp(argv[0], "show") != 0) {
		return cmd_results_new(CMD_INVALID, "Expected 'scratchpad show'");
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	if (!root->scratchpad->length) {
		return cmd_results_new(CMD_INVALID, "Scratchpad is empty");
	}

	if (config->handler_context.node_overridden) {
		struct sway_container *con = config->handler_context.container;

		// If the container is in a floating split container,
		// operate on the split container instead of the child.
		if (con && container_is_floating_or_child(con)) {
			while (con->pending.parent) {
				con = con->pending.parent;
			}
		}

		// If using criteria, this command is executed for every container which
		// matches the criteria. If this container isn't in the scratchpad,
		// we'll just silently return a success. The same is true if the
		// overridden node is not a container.
		if (!con || !con->scratchpad) {
			return cmd_results_new(CMD_SUCCESS, NULL);
		}
		scratchpad_toggle_container(con);
	} else {
		scratchpad_toggle_auto();
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
