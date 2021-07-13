#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include "sway/config.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/input/keyboard.h"
#include "sway/input/libinput.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/server.h"
#include "sway/tree/view.h"
#include "stringop.h"
#include "list.h"
#include "log.h"

#define DEFAULT_SEAT "seat0"

struct input_config *current_input_config = NULL;
struct seat_config *current_seat_config = NULL;

struct sway_seat *input_manager_current_seat(void) {
	struct sway_seat *seat = config->handler_context.seat;
	if (!seat) {
		seat = input_manager_get_default_seat();
	}
	return seat;
}

struct sway_seat *input_manager_get_default_seat(void) {
	return input_manager_get_seat(DEFAULT_SEAT, true);
}

struct sway_seat *input_manager_get_seat(const char *seat_name, bool create) {
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		if (strcmp(seat->wlr_seat->name, seat_name) == 0) {
			return seat;
		}
	}

	return create ? seat_create(seat_name) : NULL;
}

struct sway_seat *input_manager_sway_seat_from_wlr_seat(struct wlr_seat *wlr_seat) {
	struct sway_seat *seat = NULL;

	wl_list_for_each(seat, &server.input->seats, link) {
		if (seat->wlr_seat == wlr_seat) {
			return seat;
		}
	}

	return NULL;
}

char *input_device_get_identifier(struct wlr_input_device *device) {
	int vendor = device->vendor;
	int product = device->product;
	char *name = strdup(device->name ? device->name : "");
	strip_whitespace(name);

	char *p = name;
	for (; *p; ++p) {
		// There are in fact input devices with unprintable characters in its name
		if (*p == ' ' || !isprint(*p)) {
			*p = '_';
		}
	}

	const char *fmt = "%d:%d:%s";
	int len = snprintf(NULL, 0, fmt, vendor, product, name) + 1;
	char *identifier = malloc(len);
	if (!identifier) {
		sway_log(SWAY_ERROR, "Unable to allocate unique input device name");
		return NULL;
	}

	snprintf(identifier, len, fmt, vendor, product, name);
	free(name);
	return identifier;
}

static bool device_is_touchpad(struct sway_input_device *device) {
	if (device->wlr_device->type != WLR_INPUT_DEVICE_POINTER ||
			!wlr_input_device_is_libinput(device->wlr_device)) {
		return false;
	}

	struct libinput_device *libinput_device =
		wlr_libinput_get_device_handle(device->wlr_device);

	return libinput_device_config_tap_get_finger_count(libinput_device) > 0;
}

const char *input_device_get_type(struct sway_input_device *device) {
	switch (device->wlr_device->type) {
	case WLR_INPUT_DEVICE_POINTER:
		if (device_is_touchpad(device)) {
			return "touchpad";
		} else {
			return "pointer";
		}
	case WLR_INPUT_DEVICE_KEYBOARD:
		return "keyboard";
	case WLR_INPUT_DEVICE_TOUCH:
		return "touch";
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return "tablet_tool";
	case WLR_INPUT_DEVICE_TABLET_PAD:
		return "tablet_pad";
	case WLR_INPUT_DEVICE_SWITCH:
		return "switch";
	}
	return "unknown";
}

static void apply_input_type_config(struct sway_input_device *input_device) {
	const char *device_type = input_device_get_type(input_device);
	struct input_config *type_config = NULL;
	for (int i = 0; i < config->input_type_configs->length; i++) {
		struct input_config *ic = config->input_type_configs->items[i];
		if (strcmp(ic->identifier + 5, device_type) == 0) {
			type_config = ic;
			break;
		}
	}
	if (type_config == NULL) {
		return;
	}

	for (int i = 0; i < config->input_configs->length; i++) {
		struct input_config *ic = config->input_configs->items[i];
		if (strcmp(input_device->identifier, ic->identifier) == 0) {
			struct input_config *current = new_input_config(ic->identifier);
			merge_input_config(current, type_config);
			merge_input_config(current, ic);

			current->input_type = device_type;
			config->input_configs->items[i] = current;
			free_input_config(ic);
			ic = NULL;

			break;
		}
	}
}

struct sway_input_device *input_sway_device_from_wlr(
		struct wlr_input_device *device) {
	struct sway_input_device *input_device = NULL;
	wl_list_for_each(input_device, &server.input->devices, link) {
		if (input_device->wlr_device == device) {
			return input_device;
		}
	}
	return NULL;
}

static bool input_has_seat_fallback_configuration(void) {
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct seat_config *seat_config = seat_get_config(seat);
		if (seat_config && strcmp(seat_config->name, "*") != 0
				&& seat_config->fallback != -1) {
			return true;
		}
	}

	return false;
}

void input_manager_verify_fallback_seat(void) {
	struct sway_seat *seat = NULL;
	if (!input_has_seat_fallback_configuration()) {
		sway_log(SWAY_DEBUG, "no fallback seat config - creating default");
		seat = input_manager_get_default_seat();
		struct seat_config *sc = new_seat_config(seat->wlr_seat->name);
		sc->fallback = true;
		sc = store_seat_config(sc);
		input_manager_apply_seat_config(sc);
	}
}

static void handle_device_destroy(struct wl_listener *listener, void *data) {
	struct wlr_input_device *device = data;

	struct sway_input_device *input_device = input_sway_device_from_wlr(device);

	if (!sway_assert(input_device, "could not find sway device")) {
		return;
	}

	sway_log(SWAY_DEBUG, "removing device: '%s'",
		input_device->identifier);

	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_remove_device(seat, input_device);
	}

	ipc_event_input("removed", input_device);

	wl_list_remove(&input_device->link);
	wl_list_remove(&input_device->device_destroy.link);
	free(input_device->identifier);
	free(input_device);
}

static void handle_new_input(struct wl_listener *listener, void *data) {
	struct sway_input_manager *input =
		wl_container_of(listener, input, new_input);
	struct wlr_input_device *device = data;

	struct sway_input_device *input_device =
		calloc(1, sizeof(struct sway_input_device));
	if (!sway_assert(input_device, "could not allocate input device")) {
		return;
	}
	device->data = input_device;

	input_device->wlr_device = device;
	input_device->identifier = input_device_get_identifier(device);
	wl_list_insert(&input->devices, &input_device->link);

	sway_log(SWAY_DEBUG, "adding device: '%s'",
		input_device->identifier);

	apply_input_type_config(input_device);

	sway_input_configure_libinput_device(input_device);

	wl_signal_add(&device->events.destroy, &input_device->device_destroy);
	input_device->device_destroy.notify = handle_device_destroy;

	input_manager_verify_fallback_seat();

	bool added = false;
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &input->seats, link) {
		struct seat_config *seat_config = seat_get_config(seat);
		bool has_attachment = seat_config &&
			(seat_config_get_attachment(seat_config, input_device->identifier) ||
			 seat_config_get_attachment(seat_config, "*"));

		if (has_attachment) {
			seat_add_device(seat, input_device);
			added = true;
		}
	}

	if (!added) {
		wl_list_for_each(seat, &input->seats, link) {
			struct seat_config *seat_config = seat_get_config(seat);
			if (seat_config && seat_config->fallback == 1) {
				seat_add_device(seat, input_device);
				added = true;
			}
		}
	}

	if (!added) {
		sway_log(SWAY_DEBUG,
			"device '%s' is not configured on any seats",
			input_device->identifier);
	}

	ipc_event_input("added", input_device);
}

static void handle_inhibit_activate(struct wl_listener *listener, void *data) {
	struct sway_input_manager *input_manager = wl_container_of(
			listener, input_manager, inhibit_activate);
	struct sway_seat *seat;
	wl_list_for_each(seat, &input_manager->seats, link) {
		seat_set_exclusive_client(seat, input_manager->inhibit->active_client);
	}
}

static void handle_inhibit_deactivate(struct wl_listener *listener, void *data) {
	struct sway_input_manager *input_manager = wl_container_of(
			listener, input_manager, inhibit_deactivate);
	struct sway_seat *seat;
	wl_list_for_each(seat, &input_manager->seats, link) {
		seat_set_exclusive_client(seat, NULL);
		struct sway_node *previous = seat_get_focus(seat);
		if (previous) {
			// Hack to get seat to re-focus the return value of get_focus
			seat_set_focus(seat, NULL);
			seat_set_focus(seat, previous);
		}
	}
}

static void handle_keyboard_shortcuts_inhibitor_destroy(
		struct wl_listener *listener, void *data) {
	struct sway_keyboard_shortcuts_inhibitor *sway_inhibitor =
		wl_container_of(listener, sway_inhibitor, destroy);

	sway_log(SWAY_DEBUG, "Removing keyboard shortcuts inhibitor");

	// sway_seat::keyboard_shortcuts_inhibitors
	wl_list_remove(&sway_inhibitor->link);
	wl_list_remove(&sway_inhibitor->destroy.link);
	free(sway_inhibitor);
}

static void handle_keyboard_shortcuts_inhibit_new_inhibitor(
		struct wl_listener *listener, void *data) {
	struct sway_input_manager *input_manager =
		wl_container_of(listener, input_manager,
				keyboard_shortcuts_inhibit_new_inhibitor);
	struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor = data;

	sway_log(SWAY_DEBUG, "Adding keyboard shortcuts inhibitor");

	struct sway_keyboard_shortcuts_inhibitor *sway_inhibitor =
		calloc(1, sizeof(struct sway_keyboard_shortcuts_inhibitor));
	if (!sway_assert(sway_inhibitor, "could not allocate keyboard "
				"shortcuts inhibitor")) {
		return;
	}
	sway_inhibitor->inhibitor = inhibitor;

	sway_inhibitor->destroy.notify = handle_keyboard_shortcuts_inhibitor_destroy;
	wl_signal_add(&inhibitor->events.destroy, &sway_inhibitor->destroy);

	// attach inhibitor to the seat it applies to
	struct sway_seat *seat = inhibitor->seat->data;
	wl_list_insert(&seat->keyboard_shortcuts_inhibitors, &sway_inhibitor->link);

	// per-view, seat-agnostic config via criteria
	struct sway_view *view = view_from_wlr_surface(inhibitor->surface);
	enum seat_config_shortcuts_inhibit inhibit = SHORTCUTS_INHIBIT_DEFAULT;
	if (view) {
		inhibit = view->shortcuts_inhibit;
	}

	if (inhibit == SHORTCUTS_INHIBIT_DEFAULT) {
		struct seat_config *config = seat_get_config(seat);
		if (!config) {
			config = seat_get_config_by_name("*");
		}

		if (config) {
			inhibit = config->shortcuts_inhibit;
		}
	}

	if (inhibit == SHORTCUTS_INHIBIT_DISABLE) {
		/**
		 * Here we deny to honour the inhibitor by never sending the
		 * activate signal. We can not, however, destroy the inhibitor
		 * because the protocol doesn't allow for it. So it will linger
		 * until the client removes it im- or explicitly. But at least
		 * it can only be one inhibitor per surface and seat at a time.
		 *
		 * We also want to allow the user to activate the inhibitor
		 * manually later which is why we do this check here where the
		 * inhibitor is already attached to its seat and ready for use.
		 */
		return;
	}

	wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
}

void handle_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct sway_input_manager *input_manager =
		wl_container_of(listener, input_manager, virtual_keyboard_new);
	struct wlr_virtual_keyboard_v1 *keyboard = data;
	struct wlr_input_device *device = &keyboard->input_device;

	// TODO: Amend protocol to allow NULL seat
	struct sway_seat *seat = keyboard->seat ?
		input_manager_sway_seat_from_wlr_seat(keyboard->seat) :
		input_manager_get_default_seat();

	struct sway_input_device *input_device =
		calloc(1, sizeof(struct sway_input_device));
	if (!sway_assert(input_device, "could not allocate input device")) {
		return;
	}
	device->data = input_device;

	input_device->is_virtual = true;
	input_device->wlr_device = device;
	input_device->identifier = input_device_get_identifier(device);
	wl_list_insert(&input_manager->devices, &input_device->link);

	sway_log(SWAY_DEBUG, "adding virtual keyboard: '%s'",
		input_device->identifier);

	wl_signal_add(&device->events.destroy, &input_device->device_destroy);
	input_device->device_destroy.notify = handle_device_destroy;

	seat_add_device(seat, input_device);
}

void handle_virtual_pointer(struct wl_listener *listener, void *data) {
	struct sway_input_manager *input_manager =
		wl_container_of(listener, input_manager, virtual_pointer_new);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_virtual_pointer_v1 *pointer = event->new_pointer;
	struct wlr_input_device *device = &pointer->input_device;

	struct sway_seat *seat = event->suggested_seat ?
		input_manager_sway_seat_from_wlr_seat(event->suggested_seat) :
		input_manager_get_default_seat();

	struct sway_input_device *input_device =
		calloc(1, sizeof(struct sway_input_device));
	if (!sway_assert(input_device, "could not allocate input device")) {
		return;
	}
	device->data = input_device;

	input_device->is_virtual = true;
	input_device->wlr_device = device;
	input_device->identifier = input_device_get_identifier(device);
	wl_list_insert(&input_manager->devices, &input_device->link);

	sway_log(SWAY_DEBUG, "adding virtual pointer: '%s'",
		input_device->identifier);

	wl_signal_add(&device->events.destroy, &input_device->device_destroy);
	input_device->device_destroy.notify = handle_device_destroy;

	seat_add_device(seat, input_device);

	if (event->suggested_output) {
		wlr_cursor_map_input_to_output(seat->cursor->cursor, device,
			event->suggested_output);
	}
}

struct sway_input_manager *input_manager_create(struct sway_server *server) {
	struct sway_input_manager *input =
		calloc(1, sizeof(struct sway_input_manager));
	if (!input) {
		return NULL;
	}

	wl_list_init(&input->devices);
	wl_list_init(&input->seats);

	input->new_input.notify = handle_new_input;
	wl_signal_add(&server->backend->events.new_input, &input->new_input);

	input->virtual_keyboard = wlr_virtual_keyboard_manager_v1_create(
		server->wl_display);
	wl_signal_add(&input->virtual_keyboard->events.new_virtual_keyboard,
		&input->virtual_keyboard_new);
	input->virtual_keyboard_new.notify = handle_virtual_keyboard;

	input->virtual_pointer = wlr_virtual_pointer_manager_v1_create(
		server->wl_display
	);
	wl_signal_add(&input->virtual_pointer->events.new_virtual_pointer,
		&input->virtual_pointer_new);
	input->virtual_pointer_new.notify = handle_virtual_pointer;

	input->inhibit = wlr_input_inhibit_manager_create(server->wl_display);
	input->inhibit_activate.notify = handle_inhibit_activate;
	wl_signal_add(&input->inhibit->events.activate,
			&input->inhibit_activate);
	input->inhibit_deactivate.notify = handle_inhibit_deactivate;
	wl_signal_add(&input->inhibit->events.deactivate,
			&input->inhibit_deactivate);

	input->keyboard_shortcuts_inhibit =
		wlr_keyboard_shortcuts_inhibit_v1_create(server->wl_display);
	input->keyboard_shortcuts_inhibit_new_inhibitor.notify =
		handle_keyboard_shortcuts_inhibit_new_inhibitor;
	wl_signal_add(&input->keyboard_shortcuts_inhibit->events.new_inhibitor,
			&input->keyboard_shortcuts_inhibit_new_inhibitor);

	return input;
}

bool input_manager_has_focus(struct sway_node *node) {
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		if (seat_get_focus(seat) == node) {
			return true;
		}
	}

	return false;
}

void input_manager_set_focus(struct sway_node *node) {
	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_set_focus(seat, node);
		seat_consider_warp_to_focus(seat);
	}
}

/**
 * Re-translate keysyms if a change in the input config could affect them.
 */
static void retranslate_keysyms(struct input_config *input_config) {
	for (int i = 0; i < config->input_configs->length; ++i) {
		struct input_config *ic = config->input_configs->items[i];
		if (ic->xkb_layout || ic->xkb_file) {
			// this is the first config with xkb_layout or xkb_file
			if (ic->identifier == input_config->identifier) {
				translate_keysyms(ic);
			}

			return;
		}
	}
}

static void input_manager_configure_input(
		struct sway_input_device *input_device) {
	sway_input_configure_libinput_device(input_device);
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_configure_device(seat, input_device);
	}
}

void input_manager_configure_all_inputs(void) {
	struct sway_input_device *input_device = NULL;
	wl_list_for_each(input_device, &server.input->devices, link) {
		input_manager_configure_input(input_device);
	}
}

void input_manager_apply_input_config(struct input_config *input_config) {
	struct sway_input_device *input_device = NULL;
	bool wildcard = strcmp(input_config->identifier, "*") == 0;
	bool type_wildcard = strncmp(input_config->identifier, "type:", 5) == 0;
	wl_list_for_each(input_device, &server.input->devices, link) {
		bool type_matches = type_wildcard &&
			strcmp(input_device_get_type(input_device), input_config->identifier + 5) == 0;
		if (strcmp(input_device->identifier, input_config->identifier) == 0
				|| wildcard
				|| type_matches) {
			input_manager_configure_input(input_device);
		}
	}

	retranslate_keysyms(input_config);
}

void input_manager_reset_input(struct sway_input_device *input_device) {
	sway_input_reset_libinput_device(input_device);
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_reset_device(seat, input_device);
	}
}

void input_manager_reset_all_inputs(void) {
	struct sway_input_device *input_device = NULL;
	wl_list_for_each(input_device, &server.input->devices, link) {
		input_manager_reset_input(input_device);
	}

	// If there is at least one keyboard using the default keymap, repeat delay,
	// and repeat rate, then it is possible that there is a keyboard group that
	// need their keyboard disarmed.
	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct sway_keyboard_group *group;
		wl_list_for_each(group, &seat->keyboard_groups, link) {
			sway_keyboard_disarm_key_repeat(group->seat_device->keyboard);
		}
	}
}

void input_manager_apply_seat_config(struct seat_config *seat_config) {
	sway_log(SWAY_DEBUG, "applying seat config for seat %s", seat_config->name);
	if (strcmp(seat_config->name, "*") == 0) {
		struct sway_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			// Only apply the wildcard config directly if there is no seat
			// specific config
			struct seat_config *sc = seat_get_config(seat);
			if (!sc) {
				sc = seat_config;
			}
			seat_apply_config(seat, sc);
		}
	} else {
		struct sway_seat *seat =
			input_manager_get_seat(seat_config->name, true);
		if (!seat) {
			return;
		}
		seat_apply_config(seat, seat_config);
	}

	// for every device, try to add it to a seat and if no seat has it
	// attached, add it to the fallback seats.
	struct sway_input_device *input_device = NULL;
	wl_list_for_each(input_device, &server.input->devices, link) {
		list_t *seat_list = create_list();
		struct sway_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			struct seat_config *seat_config = seat_get_config(seat);
			if (!seat_config) {
				continue;
			}
			if (seat_config_get_attachment(seat_config, "*") ||
					seat_config_get_attachment(seat_config,
						input_device->identifier)) {
				list_add(seat_list, seat);
			}
		}

		if (seat_list->length) {
			wl_list_for_each(seat, &server.input->seats, link) {
				bool attached = false;
				for (int i = 0; i < seat_list->length; ++i) {
					if (seat == seat_list->items[i]) {
						attached = true;
						break;
					}
				}
				if (attached) {
					seat_add_device(seat, input_device);
				} else {
					seat_remove_device(seat, input_device);
				}
			}
		} else {
			wl_list_for_each(seat, &server.input->seats, link) {
				struct seat_config *seat_config = seat_get_config(seat);
				if (seat_config && seat_config->fallback == 1) {
					seat_add_device(seat, input_device);
				} else {
					seat_remove_device(seat, input_device);
				}
			}
		}
		list_free(seat_list);
	}
}

void input_manager_configure_xcursor(void) {
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_configure_xcursor(seat);
	}
}

struct input_config *input_device_get_config(struct sway_input_device *device) {
	struct input_config *wildcard_config = NULL;
	struct input_config *input_config = NULL;
	for (int i = 0; i < config->input_configs->length; ++i) {
		input_config = config->input_configs->items[i];
		if (strcmp(input_config->identifier, device->identifier) == 0) {
			return input_config;
		} else if (strcmp(input_config->identifier, "*") == 0) {
			wildcard_config = input_config;
		}
	}

	const char *device_type = input_device_get_type(device);
	for (int i = 0; i < config->input_type_configs->length; ++i) {
		input_config = config->input_type_configs->items[i];
		if (strcmp(input_config->identifier + 5, device_type) == 0) {
			return input_config;
		}
	}

	return wildcard_config;
}
