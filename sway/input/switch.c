#include "sway/config.h"
#include "sway/input/switch.h"
#include <wlr/types/wlr_idle.h>
#include "log.h"

struct sway_switch *sway_switch_create(struct sway_seat *seat,
		struct sway_seat_device *device) {
	struct sway_switch *switch_device =
		calloc(1, sizeof(struct sway_switch));
	if (!sway_assert(switch_device, "could not allocate switch")) {
		return NULL;
	}
	device->switch_device = switch_device;
	switch_device->seat_device = device;
	switch_device->state = WLR_SWITCH_STATE_OFF;
	wl_list_init(&switch_device->switch_toggle.link);
	sway_log(SWAY_DEBUG, "Allocated switch for device");

	return switch_device;
}

static void execute_binding(struct sway_switch *sway_switch) {
	struct sway_seat* seat = sway_switch->seat_device->sway_seat;
	bool input_inhibited = seat->exclusive_client != NULL;

	list_t *bindings = config->current_mode->switch_bindings;
	struct sway_switch_binding *matched_binding = NULL;
	for (int i = 0; i < bindings->length; ++i) {
		struct sway_switch_binding *binding = bindings->items[i];
		if (binding->type != sway_switch->type) {
			continue;
		}
		if (binding->state != WLR_SWITCH_STATE_TOGGLE &&
				binding->state != sway_switch->state) {
			continue;
		}
		if (config->reloading && (binding->state == WLR_SWITCH_STATE_TOGGLE
				|| (binding->flags & BINDING_RELOAD) == 0)) {
			continue;
		}
		bool binding_locked = binding->flags & BINDING_LOCKED;
		if (!binding_locked && input_inhibited) {
			continue;
		}

		matched_binding = binding;

		if (binding_locked == input_inhibited) {
			break;
		}
	}

	if (matched_binding) {
		struct sway_binding *dummy_binding =
			calloc(1, sizeof(struct sway_binding));
		dummy_binding->type = BINDING_SWITCH;
		dummy_binding->flags = matched_binding->flags;
		dummy_binding->command = matched_binding->command;

		seat_execute_command(seat, dummy_binding);
		free(dummy_binding);
	}
}

static void handle_switch_toggle(struct wl_listener *listener, void *data) {
	struct sway_switch *sway_switch =
			wl_container_of(listener, sway_switch, switch_toggle);
	struct wlr_event_switch_toggle *event = data;
	struct sway_seat *seat = sway_switch->seat_device->sway_seat;
	seat_idle_notify_activity(seat, IDLE_SOURCE_SWITCH);

	struct wlr_input_device *wlr_device =
		sway_switch->seat_device->input_device->wlr_device;
	char *device_identifier = input_device_get_identifier(wlr_device);
	sway_log(SWAY_DEBUG, "%s: type %d state %d", device_identifier,
			event->switch_type, event->switch_state);
	free(device_identifier);

	sway_switch->type = event->switch_type;
	sway_switch->state = event->switch_state;
	execute_binding(sway_switch);
}

void sway_switch_configure(struct sway_switch *sway_switch) {
	struct wlr_input_device *wlr_device =
		sway_switch->seat_device->input_device->wlr_device;
	wl_list_remove(&sway_switch->switch_toggle.link);
	wl_signal_add(&wlr_device->switch_device->events.toggle,
			&sway_switch->switch_toggle);
	sway_switch->switch_toggle.notify = handle_switch_toggle;
	sway_log(SWAY_DEBUG, "Configured switch for device");
}

void sway_switch_destroy(struct sway_switch *sway_switch) {
	if (!sway_switch) {
		return;
	}
	wl_list_remove(&sway_switch->switch_toggle.link);
	free(sway_switch);
}

void sway_switch_retrigger_bindings_for_all(void) {
	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct sway_seat_device *seat_device;
		wl_list_for_each(seat_device, &seat->devices, link) {
			struct sway_input_device *input_device = seat_device->input_device;
			if (input_device->wlr_device->type != WLR_INPUT_DEVICE_SWITCH) {
				continue;
			}
			execute_binding(seat_device->switch_device);
		};
	}
}
