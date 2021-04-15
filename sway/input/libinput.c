#include <float.h>
#include <libinput.h>
#include <libudev.h>
#include <limits.h>
#include <wlr/backend/libinput.h>
#include "log.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/input/input-manager.h"
#include "sway/ipc-server.h"

static void log_status(enum libinput_config_status status) {
	if (status != LIBINPUT_CONFIG_STATUS_SUCCESS) {
		sway_log(SWAY_ERROR, "Failed to apply libinput config: %s",
			libinput_config_status_to_str(status));
	}
}

static bool set_send_events(struct libinput_device *device, uint32_t mode) {
	if (libinput_device_config_send_events_get_mode(device) == mode) {
		return false;
	}
	sway_log(SWAY_DEBUG, "send_events_set_mode(%" PRIu32 ")", mode);
	log_status(libinput_device_config_send_events_set_mode(device, mode));
	return true;
}

static bool set_tap(struct libinput_device *device,
		enum libinput_config_tap_state tap) {
	if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
			libinput_device_config_tap_get_enabled(device) == tap) {
		return false;
	}
	sway_log(SWAY_DEBUG, "tap_set_enabled(%d)", tap);
	log_status(libinput_device_config_tap_set_enabled(device, tap));
	return true;
}

static bool set_tap_button_map(struct libinput_device *device,
		enum libinput_config_tap_button_map map) {
	if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
			libinput_device_config_tap_get_button_map(device) == map) {
		return false;
	}
	sway_log(SWAY_DEBUG, "tap_set_button_map(%d)", map);
	log_status(libinput_device_config_tap_set_button_map(device, map));
	return true;
}

static bool set_tap_drag(struct libinput_device *device,
		enum libinput_config_drag_state drag) {
	if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
			libinput_device_config_tap_get_drag_enabled(device) == drag) {
		return false;
	}
	sway_log(SWAY_DEBUG, "tap_set_drag_enabled(%d)", drag);
	log_status(libinput_device_config_tap_set_drag_enabled(device, drag));
	return true;
}

static bool set_tap_drag_lock(struct libinput_device *device,
		enum libinput_config_drag_lock_state lock) {
	if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
			libinput_device_config_tap_get_drag_lock_enabled(device) == lock) {
		return false;
	}
	sway_log(SWAY_DEBUG, "tap_set_drag_lock_enabled(%d)", lock);
	log_status(libinput_device_config_tap_set_drag_lock_enabled(device, lock));
	return true;
}

static bool set_accel_speed(struct libinput_device *device, double speed) {
	if (!libinput_device_config_accel_is_available(device) ||
			libinput_device_config_accel_get_speed(device) == speed) {
		return false;
	}
	sway_log(SWAY_DEBUG, "accel_set_speed(%f)", speed);
	log_status(libinput_device_config_accel_set_speed(device, speed));
	return true;
}

static bool set_accel_profile(struct libinput_device *device,
		enum libinput_config_accel_profile profile) {
	if (!libinput_device_config_accel_is_available(device) ||
			libinput_device_config_accel_get_profile(device) == profile) {
		return false;
	}
	sway_log(SWAY_DEBUG, "accel_set_profile(%d)", profile);
	log_status(libinput_device_config_accel_set_profile(device, profile));
	return true;
}

static bool set_natural_scroll(struct libinput_device *d, bool n) {
	if (!libinput_device_config_scroll_has_natural_scroll(d) ||
			libinput_device_config_scroll_get_natural_scroll_enabled(d) == n) {
		return false;
	}
	sway_log(SWAY_DEBUG, "scroll_set_natural_scroll(%d)", n);
	log_status(libinput_device_config_scroll_set_natural_scroll_enabled(d, n));
	return true;
}

static bool set_left_handed(struct libinput_device *device, bool left) {
	if (!libinput_device_config_left_handed_is_available(device) ||
			libinput_device_config_left_handed_get(device) == left) {
		return false;
	}
	sway_log(SWAY_DEBUG, "left_handed_set(%d)", left);
	log_status(libinput_device_config_left_handed_set(device, left));
	return true;
}

static bool set_click_method(struct libinput_device *device,
		enum libinput_config_click_method method) {
	uint32_t click = libinput_device_config_click_get_methods(device);
	if ((click & ~LIBINPUT_CONFIG_CLICK_METHOD_NONE) == 0 ||
			libinput_device_config_click_get_method(device) == method) {
		return false;
	}
	sway_log(SWAY_DEBUG, "click_set_method(%d)", method);
	log_status(libinput_device_config_click_set_method(device, method));
	return true;
}

static bool set_middle_emulation(struct libinput_device *dev,
		enum libinput_config_middle_emulation_state mid) {
	if (!libinput_device_config_middle_emulation_is_available(dev) ||
			libinput_device_config_middle_emulation_get_enabled(dev) == mid) {
		return false;
	}
	sway_log(SWAY_DEBUG, "middle_emulation_set_enabled(%d)", mid);
	log_status(libinput_device_config_middle_emulation_set_enabled(dev, mid));
	return true;
}

static bool set_scroll_method(struct libinput_device *device,
		enum libinput_config_scroll_method method) {
	uint32_t scroll = libinput_device_config_scroll_get_methods(device);
	if ((scroll & ~LIBINPUT_CONFIG_SCROLL_NO_SCROLL) == 0 ||
			libinput_device_config_scroll_get_method(device) == method) {
		return false;
	}
	sway_log(SWAY_DEBUG, "scroll_set_method(%d)", method);
	log_status(libinput_device_config_scroll_set_method(device, method));
	return true;
}

static bool set_scroll_button(struct libinput_device *dev, uint32_t button) {
	uint32_t scroll = libinput_device_config_scroll_get_methods(dev);
	if ((scroll & ~LIBINPUT_CONFIG_SCROLL_NO_SCROLL) == 0 ||
			libinput_device_config_scroll_get_button(dev) == button) {
		return false;
	}
	sway_log(SWAY_DEBUG, "scroll_set_button(%" PRIu32 ")", button);
	log_status(libinput_device_config_scroll_set_button(dev, button));
	return true;
}

static bool set_dwt(struct libinput_device *device, bool dwt) {
	if (!libinput_device_config_dwt_is_available(device) ||
			libinput_device_config_dwt_get_enabled(device) == dwt) {
		return false;
	}
	sway_log(SWAY_DEBUG, "dwt_set_enabled(%d)", dwt);
	log_status(libinput_device_config_dwt_set_enabled(device, dwt));
	return true;
}

static bool set_calibration_matrix(struct libinput_device *dev, float mat[6]) {
	if (!libinput_device_config_calibration_has_matrix(dev)) {
		return false;
	}
	bool changed = false;
	float current[6];
	libinput_device_config_calibration_get_matrix(dev, current);
	for (int i = 0; i < 6; i++) {
		if (current[i] != mat[i]) {
			changed = true;
			break;
		}
	}
	if (changed) {
		sway_log(SWAY_DEBUG, "calibration_set_matrix(%f, %f, %f, %f, %f, %f)",
				mat[0], mat[1], mat[2], mat[3], mat[4], mat[5]);
		log_status(libinput_device_config_calibration_set_matrix(dev, mat));
	}
	return changed;
}

void sway_input_configure_libinput_device(struct sway_input_device *input_device) {
	struct input_config *ic = input_device_get_config(input_device);
	if (!ic || !wlr_input_device_is_libinput(input_device->wlr_device)) {
		return;
	}

	struct libinput_device *device =
		wlr_libinput_get_device_handle(input_device->wlr_device);
	sway_log(SWAY_DEBUG, "sway_input_configure_libinput_device('%s' on '%s')",
			ic->identifier, input_device->identifier);

	bool changed = false;
	if (ic->mapped_to_output &&
			!output_by_name_or_id(ic->mapped_to_output)) {
		sway_log(SWAY_DEBUG,
				"%s '%s' is mapped to offline output '%s'; disabling input",
				ic->input_type, ic->identifier, ic->mapped_to_output);
		changed |= set_send_events(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	} else if (ic->send_events != INT_MIN) {
		changed |= set_send_events(device, ic->send_events);
	} else {
		// Have to reset to the default mode here, otherwise if ic->send_events
		// is unset and a mapped output just came online after being disabled,
		// we'd remain stuck sending no events.
		changed |= set_send_events(device,
			libinput_device_config_send_events_get_default_mode(device));
	}

	if (ic->tap != INT_MIN) {
		changed |= set_tap(device, ic->tap);
	}
	if (ic->tap_button_map != INT_MIN) {
		changed |= set_tap_button_map(device, ic->tap_button_map);
	}
	if (ic->drag != INT_MIN) {
		changed |= set_tap_drag(device, ic->drag);
	}
	if (ic->drag_lock != INT_MIN) {
		changed |= set_tap_drag_lock(device, ic->drag_lock);
	}
	if (ic->pointer_accel != FLT_MIN) {
		changed |= set_accel_speed(device, ic->pointer_accel);
	}
	if (ic->accel_profile != INT_MIN) {
		changed |= set_accel_profile(device, ic->accel_profile);
	}
	if (ic->natural_scroll != INT_MIN) {
		changed |= set_natural_scroll(device, ic->natural_scroll);
	}
	if (ic->left_handed != INT_MIN) {
		changed |= set_left_handed(device, ic->left_handed);
	}
	if (ic->click_method != INT_MIN) {
		changed |= set_click_method(device, ic->click_method);
	}
	if (ic->middle_emulation != INT_MIN) {
		changed |= set_middle_emulation(device, ic->middle_emulation);
	}
	if (ic->scroll_method != INT_MIN) {
		changed |= set_scroll_method(device, ic->scroll_method);
	}
	if (ic->scroll_button != INT_MIN) {
		changed |= set_scroll_button(device, ic->scroll_button);
	}
	if (ic->dwt != INT_MIN) {
		changed |= set_dwt(device, ic->dwt);
	}
	if (ic->calibration_matrix.configured) {
		changed |= set_calibration_matrix(device, ic->calibration_matrix.matrix);
	}

	if (changed) {
		ipc_event_input("libinput_config", input_device);
	}
}

void sway_input_reset_libinput_device(struct sway_input_device *input_device) {
	if (!wlr_input_device_is_libinput(input_device->wlr_device)) {
		return;
	}

	struct libinput_device *device =
		wlr_libinput_get_device_handle(input_device->wlr_device);
	sway_log(SWAY_DEBUG, "sway_input_reset_libinput_device(%s)",
		input_device->identifier);
	bool changed = false;

	changed |= set_send_events(device,
		libinput_device_config_send_events_get_default_mode(device));
	changed |= set_tap(device,
		libinput_device_config_tap_get_default_enabled(device));
	changed |= set_tap_button_map(device,
		libinput_device_config_tap_get_default_button_map(device));
	changed |= set_tap_drag(device,
		libinput_device_config_tap_get_default_drag_enabled(device));
	changed |= set_tap_drag_lock(device,
		libinput_device_config_tap_get_default_drag_lock_enabled(device));
	changed |= set_accel_speed(device,
		libinput_device_config_accel_get_default_speed(device));
	changed |= set_accel_profile(device,
		libinput_device_config_accel_get_default_profile(device));
	changed |= set_natural_scroll(device,
		libinput_device_config_scroll_get_default_natural_scroll_enabled(
		device));
	changed |= set_left_handed(device,
		libinput_device_config_left_handed_get_default(device));
	changed |= set_click_method(device,
		libinput_device_config_click_get_default_method(device));
	changed |= set_middle_emulation(device,
		libinput_device_config_middle_emulation_get_default_enabled(device));
	changed |= set_scroll_method(device,
		libinput_device_config_scroll_get_default_method(device));
	changed |= set_scroll_button(device,
		libinput_device_config_scroll_get_default_button(device));
	changed |= set_dwt(device,
		libinput_device_config_dwt_get_default_enabled(device));

	float matrix[6];
	libinput_device_config_calibration_get_default_matrix(device, matrix);
	changed |= set_calibration_matrix(device, matrix);

	if (changed) {
		ipc_event_input("libinput_config", input_device);
	}
}

bool sway_libinput_device_is_builtin(struct sway_input_device *sway_device) {
	if (!wlr_input_device_is_libinput(sway_device->wlr_device)) {
		return false;
	}

	struct libinput_device *device =
		wlr_libinput_get_device_handle(sway_device->wlr_device);
	struct udev_device *udev_device =
		libinput_device_get_udev_device(device);
	if (!udev_device) {
		return false;
	}

	const char *id_path = udev_device_get_property_value(udev_device, "ID_PATH");
	if (!id_path) {
		return false;
	}

	const char prefix[] = "platform-";
	return strncmp(id_path, prefix, strlen(prefix)) == 0;
}
