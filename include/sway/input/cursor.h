#ifndef _SWAY_INPUT_CURSOR_H
#define _SWAY_INPUT_CURSOR_H
#include <stdbool.h>
#include <stdint.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_surface.h>
#include "sway/input/seat.h"
#include "config.h"

#define SWAY_CURSOR_PRESSED_BUTTONS_CAP 32

#define SWAY_SCROLL_UP KEY_MAX + 1
#define SWAY_SCROLL_DOWN KEY_MAX + 2
#define SWAY_SCROLL_LEFT KEY_MAX + 3
#define SWAY_SCROLL_RIGHT KEY_MAX + 4

struct sway_cursor {
	struct sway_seat *seat;
	struct wlr_cursor *cursor;
	struct {
		double x, y;
		struct sway_node *node;
	} previous;
	struct wlr_xcursor_manager *xcursor_manager;
	struct wl_list tablets;
	struct wl_list tablet_pads;

	const char *image;
	struct wl_client *image_client;
	struct wlr_surface *image_surface;
	int hotspot_x, hotspot_y;

	struct wlr_pointer_constraint_v1 *active_constraint;
	pixman_region32_t confine; // invalid if active_constraint == NULL
	bool active_confine_requires_warp;

	struct wlr_pointer_gestures_v1 *pointer_gestures;
	struct wl_listener pinch_begin;
	struct wl_listener pinch_update;
	struct wl_listener pinch_end;
	struct wl_listener swipe_begin;
	struct wl_listener swipe_update;
	struct wl_listener swipe_end;

	struct wl_listener motion;
	struct wl_listener motion_absolute;
	struct wl_listener button;
	struct wl_listener axis;
	struct wl_listener frame;

	struct wl_listener touch_down;
	struct wl_listener touch_up;
	struct wl_listener touch_motion;
	struct wl_listener touch_frame;
	bool simulating_pointer_from_touch;
	bool pointer_touch_up;
	int32_t pointer_touch_id;

	struct wl_listener tool_axis;
	struct wl_listener tool_tip;
	struct wl_listener tool_proximity;
	struct wl_listener tool_button;
	bool simulating_pointer_from_tool_tip;
	uint32_t tool_buttons;

	struct wl_listener request_set_cursor;
	struct wl_listener image_surface_destroy;

	struct wl_listener constraint_commit;

	struct wl_event_source *hide_source;
	bool hidden;
	// This field is just a cache of the field in seat_config in order to avoid
	// costly seat_config lookups on every keypress. HIDE_WHEN_TYPING_DEFAULT
	// indicates that there is no cached value.
	enum seat_config_hide_cursor_when_typing hide_when_typing;

	size_t pressed_button_count;
};

struct sway_node;

struct sway_node *node_at_coords(
		struct sway_seat *seat, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);

void sway_cursor_destroy(struct sway_cursor *cursor);
struct sway_cursor *sway_cursor_create(struct sway_seat *seat);

/**
 * "Rebase" a cursor on top of whatever view is underneath it.
 *
 * This chooses a cursor icon and sends a motion event to the surface.
 */
void cursor_rebase(struct sway_cursor *cursor);
void cursor_rebase_all(void);
void cursor_update_image(struct sway_cursor *cursor, struct sway_node *node);

void cursor_handle_activity_from_idle_source(struct sway_cursor *cursor,
		enum sway_input_idle_source idle_source);
void cursor_handle_activity_from_device(struct sway_cursor *cursor,
		struct wlr_input_device *device);
void cursor_unhide(struct sway_cursor *cursor);
int cursor_get_timeout(struct sway_cursor *cursor);
void cursor_notify_key_press(struct sway_cursor *cursor);

void dispatch_cursor_button(struct sway_cursor *cursor,
	struct wlr_input_device *device, uint32_t time_msec, uint32_t button,
	enum wlr_button_state state);

void dispatch_cursor_axis(struct sway_cursor *cursor,
		struct wlr_event_pointer_axis *event);

void cursor_set_image(struct sway_cursor *cursor, const char *image,
	struct wl_client *client);

void cursor_set_image_surface(struct sway_cursor *cursor,
		struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y,
		struct wl_client *client);

void cursor_warp_to_container(struct sway_cursor *cursor,
	struct sway_container *container, bool force);

void cursor_warp_to_workspace(struct sway_cursor *cursor,
		struct sway_workspace *workspace);


void sway_cursor_constrain(struct sway_cursor *cursor,
	struct wlr_pointer_constraint_v1 *constraint);

uint32_t get_mouse_bindsym(const char *name, char **error);

uint32_t get_mouse_bindcode(const char *name, char **error);

// Considers both bindsym and bindcode
uint32_t get_mouse_button(const char *name, char **error);

const char *get_mouse_button_name(uint32_t button);

#endif
