#include <string.h>
#include "log.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"

struct cmd_results *cmd_shortcuts_inhibitor(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "shortcuts_inhibitor", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	struct sway_container *con = config->handler_context.container;
	if (!con || !con->view) {
		return cmd_results_new(CMD_INVALID,
				"Only views can have shortcuts inhibitors");
	}

	struct sway_view *view = con->view;
	if (strcmp(argv[0], "enable") == 0) {
		view->shortcuts_inhibit = SHORTCUTS_INHIBIT_ENABLE;
	} else if (strcmp(argv[0], "disable") == 0) {
		view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;

		struct sway_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			struct sway_keyboard_shortcuts_inhibitor *sway_inhibitor =
				keyboard_shortcuts_inhibitor_get_for_surface(
						seat, view->surface);
			if (!sway_inhibitor) {
				continue;
			}

			ipc_event_keyboard_shortcuts_inhibitor(
					sway_inhibitor, "deactivate");
			wlr_keyboard_shortcuts_inhibitor_v1_deactivate(
					sway_inhibitor->inhibitor);
			sway_log(SWAY_DEBUG, "Deactivated keyboard shortcuts "
					"inhibitor for seat %s on view",
					seat->wlr_seat->name);
		}
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected `shortcuts_inhibitor enable|disable`");
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
