#ifndef _SWAYBAR_TRAY_MENU_H
#define _SWAYBAR_TRAY_MENU_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "pool-buffer.h"
#include "xdg-shell-client-protocol.h"

struct swaybar_tray;
struct swaybar_output;
struct swaybar_sni;
struct wl_seat;


/* MENU */

struct swaybar_menu_item {
	struct swaybar_sni *sni;
	struct swaybar_menu_item *parent;

	int32_t id;

	bool is_separator; // instead of type
	char *label;
	bool enabled;
	bool visible;
	char *icon_name;
	cairo_surface_t *icon;
	cairo_surface_t *icon_data;

	enum {
		MENU_NONE,
		MENU_CHECKMARK,
		MENU_RADIO
	} toggle_type;
	int toggle_state;

	list_t *children; // struct swaybar_menu_item *
};

void destroy_menu(struct swaybar_menu_item *menu);


/* POPUP */

struct swaybar_popup_hotspot {
	int y;
	struct swaybar_menu_item *item;
};

struct swaybar_popup_surface {
	struct swaybar_menu_item *item;
	struct swaybar_popup_surface *child;
	list_t *hotspots; // struct swaybar_popup_hotspot *

	struct xdg_popup *xdg_popup;
	struct xdg_surface *xdg_surface;
	struct wl_surface *surface;
	struct pool_buffer buffers[2];
	struct pool_buffer *current_buffer;
};

struct swaybar_popup {
	struct swaybar_tray *tray;
	struct swaybar_sni *sni;
	struct swaybar_output *output;
	struct wl_seat *seat;
	struct xdg_wm_base *wm_base;

	struct swaybar_popup_surface *popup_surface;
	struct swaybar_popup_surface *pointer_focus;
	struct swaybar_popup_hotspot **last_hover;

	// used to track clicks across callbacks
	uint32_t serial;
	int x, y;
};

void open_popup(struct swaybar_sni *sni, struct swaybar_output *output,
		struct wl_seat *seat, uint32_t serial, int x, int y);
void destroy_popup(struct swaybar_popup *popup);


/* INPUT HOOKS
 * These functions are called at the start of their respective counterparts in
 * input.c, returning true if the event occurs on the popup instead of the bar
 */

bool popup_pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y);
bool popup_pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface);
bool popup_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y);
bool popup_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
bool popup_pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value);

bool popup_touch_down(void *data, struct wl_touch *wl_touch, uint32_t serial,
		uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t _x,
		wl_fixed_t _y);

#endif
