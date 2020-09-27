#ifndef _SWAYBAR_INPUT_H
#define _SWAYBAR_INPUT_H

#include <wayland-client.h>
#include <stdbool.h>
#include "list.h"

#define SWAY_SCROLL_UP KEY_MAX + 1
#define SWAY_SCROLL_DOWN KEY_MAX + 2
#define SWAY_SCROLL_LEFT KEY_MAX + 3
#define SWAY_SCROLL_RIGHT KEY_MAX + 4

#define SWAY_CONTINUOUS_SCROLL_TIMEOUT 1000
#define SWAY_CONTINUOUS_SCROLL_THRESHOLD 10000

struct swaybar;
struct swaybar_output;

struct swaybar_pointer {
	struct wl_pointer *pointer;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor_image *cursor_image;
	struct wl_surface *cursor_surface;
	struct swaybar_output *current;
	double x, y;
	uint32_t serial;
};

struct touch_slot {
	int32_t id;
	uint32_t time;
	struct swaybar_output *output;
	double start_x, start_y;
	double x, y;
};

struct swaybar_touch {
	struct wl_touch *touch;
	struct touch_slot slots[16];
};

enum hotspot_event_handling {
	HOTSPOT_IGNORE,
	HOTSPOT_PROCESS,
};

struct swaybar_hotspot {
	struct wl_list link; // swaybar_output::hotspots
	int x, y, width, height;
	enum hotspot_event_handling (*callback)(struct swaybar_output *output,
		struct swaybar_hotspot *hotspot, struct wl_seat *seat, uint32_t serial,
		double x, double y, uint32_t button, void *data);
	void (*destroy)(void *data);
	void *data;
};

struct swaybar_scroll_axis {
	wl_fixed_t value;
	uint32_t discrete_steps;
	uint32_t update_time;
};

struct swaybar_seat {
	struct swaybar *bar;
	uint32_t wl_name;
	struct wl_seat *wl_seat;
	struct swaybar_pointer pointer;
	struct swaybar_touch touch;
	struct wl_list link; // swaybar_seat:link
	struct swaybar_scroll_axis axis[2];
};

extern const struct wl_seat_listener seat_listener;

void update_cursor(struct swaybar_seat *seat);

uint32_t event_to_x11_button(uint32_t event);

void free_hotspots(struct wl_list *list);

void swaybar_seat_free(struct swaybar_seat *seat);

#endif
