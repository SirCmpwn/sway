#include "log.h"
#include "sway/commands.h"
#include "sway/input/seat.h"
#include "sway/input/input-manager.h"
#include "sway/ipc-server.h"
#include "util.h"

static struct cmd_results *handle_action(struct seat_config *sc,
		struct sway_seat *seat, const char *action) {
	struct sway_keyboard_shortcuts_inhibitor *sway_inhibitor = NULL;
	if (strcmp(action, "disable") == 0) {
		sc->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;

		wl_list_for_each(sway_inhibitor,
				&seat->keyboard_shortcuts_inhibitors, link) {
			ipc_event_keyboard_shortcuts_inhibitor(
					sway_inhibitor, "deactivate");
			wlr_keyboard_shortcuts_inhibitor_v1_deactivate(
					sway_inhibitor->inhibitor);
		}

		sway_log(SWAY_DEBUG, "Deactivated all keyboard shortcuts inhibitors");
	} else {
		sway_inhibitor = keyboard_shortcuts_inhibitor_get_for_focused_surface(seat);
		if (!sway_inhibitor) {
			return cmd_results_new(CMD_FAILURE,
					"No inhibitor found for focused surface");
		}

		struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor =
			sway_inhibitor->inhibitor;
		bool inhibit;
		if (strcmp(action, "activate") == 0) {
			inhibit = true;
		} else if (strcmp(action, "deactivate") == 0) {
			inhibit = false;
		} else if (strcmp(action, "toggle") == 0) {
			inhibit = !inhibitor->active;
		} else {
			return cmd_results_new(CMD_INVALID, "Expected enable|"
					"disable|activate|deactivate|toggle");
		}

		if (inhibit) {
			ipc_event_keyboard_shortcuts_inhibitor(
					sway_inhibitor, "activate");
			wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
		} else {
			ipc_event_keyboard_shortcuts_inhibitor(
					sway_inhibitor, "deactivate");
			wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor);
		}

		sway_log(SWAY_DEBUG, "%sctivated keyboard shortcuts inhibitor",
				inhibit ? "A" : "Dea");
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

// shortcuts_inhibitor [enable|disable|activate|deactivate|toggle]
struct cmd_results *seat_cmd_shortcuts_inhibitor(int argc, char **argv) {
	struct cmd_results *error =
		checkarg(argc, "shortcuts_inhibitor", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}

	struct seat_config *sc = config->handler_context.seat_config;
	if (!sc) {
		return cmd_results_new(CMD_FAILURE, "No seat defined");
	}

	if (strcmp(argv[0], "enable") == 0) {
		sc->shortcuts_inhibit = SHORTCUTS_INHIBIT_ENABLE;
	// at runtime disable is an action that also deactivates all active
	// inhibitors handled in handle_action()
	} else if (strcmp(argv[0], "disable") == 0 && !config->active) {
		sc->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;
	} else if (!config->active) {
		return cmd_results_new(CMD_INVALID, "only enable and disable "
				"can be used in the config");
	} else {
		if (strcmp(sc->name, "*") != 0) {
			struct sway_seat *seat = input_manager_get_seat(sc->name, false);
			if (!seat) {
				return cmd_results_new(CMD_FAILURE,
						"Seat %s does not exist", sc->name);
			}
			error = handle_action(sc, seat, argv[0]);
		} else {
			struct sway_seat *seat = NULL;
			wl_list_for_each(seat, &server.input->seats, link) {
				error = handle_action(sc, seat, argv[0]);
				if (error && error->status != CMD_SUCCESS) {
					break;
				}
			}
		}
	}

	return error ? error : cmd_results_new(CMD_SUCCESS, NULL);
}
