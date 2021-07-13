#ifndef _SWAY_INPUT_INPUT_MANAGER_H
#define _SWAY_INPUT_INPUT_MANAGER_H
#include <libinput.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include "sway/server.h"
#include "sway/config.h"
#include "list.h"

struct sway_input_device {
	char *identifier;
	struct wlr_input_device *wlr_device;
	struct wl_list link;
	struct wl_listener device_destroy;
	bool is_virtual;
};

struct sway_input_manager {
	struct wl_list devices;
	struct wl_list seats;

	struct wlr_input_inhibit_manager *inhibit;
	struct wlr_keyboard_shortcuts_inhibit_manager_v1 *keyboard_shortcuts_inhibit;
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard;
	struct wlr_virtual_pointer_manager_v1 *virtual_pointer;

	struct wl_listener new_input;
	struct wl_listener inhibit_activate;
	struct wl_listener inhibit_deactivate;
	struct wl_listener keyboard_shortcuts_inhibit_new_inhibitor;
	struct wl_listener virtual_keyboard_new;
	struct wl_listener virtual_pointer_new;
};

struct sway_input_manager *input_manager_create(struct sway_server *server);

bool input_manager_has_focus(struct sway_node *node);

void input_manager_set_focus(struct sway_node *node);

void input_manager_configure_xcursor(void);

void input_manager_apply_input_config(struct input_config *input_config);

void input_manager_configure_all_inputs(void);

void input_manager_reset_input(struct sway_input_device *input_device);

void input_manager_reset_all_inputs(void);

void input_manager_apply_seat_config(struct seat_config *seat_config);

struct sway_seat *input_manager_get_default_seat(void);

struct sway_seat *input_manager_get_seat(const char *seat_name, bool create);

/**
 * If none of the seat configs have a fallback setting (either true or false),
 * create the default seat (if needed) and set it as the fallback
 */
void input_manager_verify_fallback_seat(void);

/**
 * Gets the last seat the user interacted with
 */
struct sway_seat *input_manager_current_seat(void);

struct input_config *input_device_get_config(struct sway_input_device *device);

char *input_device_get_identifier(struct wlr_input_device *device);

const char *input_device_get_type(struct sway_input_device *device);

struct sway_input_device *input_sway_device_from_wlr(struct wlr_input_device *device);

#endif
