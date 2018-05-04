#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/layout.h"
#include "list.h"

struct cmd_results *cmd_floating(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "floating", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	struct sway_container *container =
		config->handler_context.current_container;
	if (container->type != C_VIEW) {
		// TODO: This doesn't strictly speaking have to be true
		return cmd_results_new(CMD_INVALID, "float", "Only views can float");
	}

	bool wants_floating;
	if (strcasecmp(argv[0], "enable") == 0) {
		wants_floating = true;
	} else if (strcasecmp(argv[0], "disable") == 0) {
		wants_floating = false;
	} else if (strcasecmp(argv[0], "toggle") == 0) {
		wants_floating = !container->is_floating;
	} else {
		return cmd_results_new(CMD_FAILURE, "floating",
			"Expected 'floating <enable|disable|toggle>");
	}

	// Change from tiled to floating
	if (!container->is_floating && wants_floating) {
		struct sway_container *workspace = container_parent(
				container, C_WORKSPACE);
		container_remove_child(container);
		container_add_floating(workspace, container);
		seat_set_focus(config->handler_context.seat, container);
		arrange_workspace(workspace);
	} else if (container->is_floating && !wants_floating) {
		// TODO
	}

	return cmd_results_new(CMD_SUCCESS, NULL, NULL);
}
