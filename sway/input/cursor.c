#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <math.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <errno.h>
#include <time.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/region.h>
#include "config.h"
#include "log.h"
#include "util.h"
#include "sway/commands.h"
#include "sway/desktop.h"
#include "sway/input/cursor.h"
#include "sway/input/keyboard.h"
#include "sway/input/tablet.h"
#include "sway/layers.h"
#include "sway/output.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

static uint32_t get_current_time_msec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static struct wlr_surface *layer_surface_at(struct sway_output *output,
		struct wl_list *layer, double ox, double oy, double *sx, double *sy) {
	struct sway_layer_surface *sway_layer;
	wl_list_for_each_reverse(sway_layer, layer, link) {
		double _sx = ox - sway_layer->geo.x;
		double _sy = oy - sway_layer->geo.y;
		struct wlr_surface *sub = wlr_layer_surface_v1_surface_at(
			sway_layer->layer_surface, _sx, _sy, sx, sy);
		if (sub) {
			return sub;
		}
	}
	return NULL;
}

static bool surface_is_xdg_popup(struct wlr_surface *surface) {
    if (wlr_surface_is_xdg_surface(surface)) {
        struct wlr_xdg_surface *xdg_surface =
            wlr_xdg_surface_from_wlr_surface(surface);
        return xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP;
    }
    return false;
}

static struct wlr_surface *layer_surface_popup_at(struct sway_output *output,
		struct wl_list *layer, double ox, double oy, double *sx, double *sy) {
	struct sway_layer_surface *sway_layer;
	wl_list_for_each_reverse(sway_layer, layer, link) {
		double _sx = ox - sway_layer->geo.x;
		double _sy = oy - sway_layer->geo.y;
		struct wlr_surface *sub = wlr_layer_surface_v1_surface_at(
			sway_layer->layer_surface, _sx, _sy, sx, sy);
		if (sub && surface_is_xdg_popup(sub)) {
			return sub;
		}
	}
	return NULL;
}

/**
 * Returns the node at the cursor's position. If there is a surface at that
 * location, it is stored in **surface (it may not be a view).
 */
struct sway_node *node_at_coords(
		struct sway_seat *seat, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	// check for unmanaged views first
#if HAVE_XWAYLAND
	struct wl_list *unmanaged = &root->xwayland_unmanaged;
	struct sway_xwayland_unmanaged *unmanaged_surface;
	wl_list_for_each_reverse(unmanaged_surface, unmanaged, link) {
		struct wlr_xwayland_surface *xsurface =
			unmanaged_surface->wlr_xwayland_surface;

		double _sx = lx - unmanaged_surface->lx;
		double _sy = ly - unmanaged_surface->ly;
		if (wlr_surface_point_accepts_input(xsurface->surface, _sx, _sy)) {
			*surface = xsurface->surface;
			*sx = _sx;
			*sy = _sy;
			return NULL;
		}
	}
#endif
	// find the output the cursor is on
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
			root->output_layout, lx, ly);
	if (wlr_output == NULL) {
		return NULL;
	}
	struct sway_output *output = wlr_output->data;
	if (!output || !output->enabled) {
		// output is being destroyed or is being enabled
		return NULL;
	}
	double ox = lx, oy = ly;
	wlr_output_layout_output_coords(root->output_layout, wlr_output, &ox, &oy);

	if (root->fullscreen_global) {
		// Try fullscreen container
		struct sway_container *con = tiling_container_at(
				&root->fullscreen_global->node, lx, ly, surface, sx, sy);
		if (con) {
			return &con->node;
		}
		return NULL;
	}

	// find the focused workspace on the output for this seat
	struct sway_workspace *ws = output_get_active_workspace(output);
	if (!ws) {
		return NULL;
	}

	if ((*surface = layer_surface_at(output,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
				ox, oy, sx, sy))) {
		return NULL;
	}
	if (ws->fullscreen) {
		// Try transient containers
		for (int i = 0; i < ws->floating->length; ++i) {
			struct sway_container *floater = ws->floating->items[i];
			if (container_is_transient_for(floater, ws->fullscreen)) {
				struct sway_container *con = tiling_container_at(
						&floater->node, lx, ly, surface, sx, sy);
				if (con) {
					return &con->node;
				}
			}
		}
		// Try fullscreen container
		struct sway_container *con =
			tiling_container_at(&ws->fullscreen->node, lx, ly, surface, sx, sy);
		if (con) {
			return &con->node;
		}
		return NULL;
	}
	if ((*surface = layer_surface_popup_at(output,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
				ox, oy, sx, sy))) {
		return NULL;
	}
	if ((*surface = layer_surface_popup_at(output,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
				ox, oy, sx, sy))) {
		return NULL;
	}
	if ((*surface = layer_surface_popup_at(output,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
				ox, oy, sx, sy))) {
		return NULL;
	}
	if ((*surface = layer_surface_at(output,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
				ox, oy, sx, sy))) {
		return NULL;
	}

	struct sway_container *c;
	if ((c = container_at(ws, lx, ly, surface, sx, sy))) {
		return &c->node;
	}

	if ((*surface = layer_surface_at(output,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
				ox, oy, sx, sy))) {
		return NULL;
	}
	if ((*surface = layer_surface_at(output,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
				ox, oy, sx, sy))) {
		return NULL;
	}

	return &ws->node;
}

void cursor_rebase(struct sway_cursor *cursor) {
	uint32_t time_msec = get_current_time_msec();
	seatop_rebase(cursor->seat, time_msec);
}

void cursor_rebase_all(void) {
	if (!root->outputs->length) {
		return;
	}

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		cursor_rebase(seat->cursor);
	}
}

void cursor_update_image(struct sway_cursor *cursor,
		struct sway_node *node) {
	if (node && node->type == N_CONTAINER) {
		// Try a node's resize edge
		enum wlr_edges edge = find_resize_edge(node->sway_container, NULL, cursor);
		if (edge == WLR_EDGE_NONE) {
			cursor_set_image(cursor, "left_ptr", NULL);
		} else if (container_is_floating(node->sway_container)) {
			cursor_set_image(cursor, wlr_xcursor_get_resize_name(edge), NULL);
		} else {
			if (edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) {
				cursor_set_image(cursor, "col-resize", NULL);
			} else {
				cursor_set_image(cursor, "row-resize", NULL);
			}
		}
	} else {
		cursor_set_image(cursor, "left_ptr", NULL);
	}
}

static void cursor_hide(struct sway_cursor *cursor) {
	wlr_cursor_set_image(cursor->cursor, NULL, 0, 0, 0, 0, 0, 0);
	cursor->hidden = true;
	wlr_seat_pointer_notify_clear_focus(cursor->seat->wlr_seat);
}

static int hide_notify(void *data) {
	struct sway_cursor *cursor = data;
	cursor_hide(cursor);
	return 1;
}

int cursor_get_timeout(struct sway_cursor *cursor) {
	if (cursor->pressed_button_count > 0) {
		// Do not hide cursor unless all buttons are released
		return 0;
	}

	struct seat_config *sc = seat_get_config(cursor->seat);
	if (!sc) {
		sc = seat_get_config_by_name("*");
	}
	int timeout = sc ? sc->hide_cursor_timeout : 0;
	if (timeout < 0) {
		timeout = 0;
	}
	return timeout;
}

void cursor_notify_key_press(struct sway_cursor *cursor) {
	if (cursor->hidden) {
		return;
	}

	if (cursor->hide_when_typing == HIDE_WHEN_TYPING_DEFAULT) {
		// No cached value, need to lookup in the seat_config
		const struct seat_config *seat_config = seat_get_config(cursor->seat);
		if (!seat_config) {
			seat_config = seat_get_config_by_name("*");
			if (!seat_config) {
				return;
			}
		}
		cursor->hide_when_typing = seat_config->hide_cursor_when_typing;
		// The default is currently disabled
		if (cursor->hide_when_typing == HIDE_WHEN_TYPING_DEFAULT) {
			cursor->hide_when_typing = HIDE_WHEN_TYPING_DISABLE;
		}
	}

	if (cursor->hide_when_typing == HIDE_WHEN_TYPING_ENABLE) {
		cursor_hide(cursor);
	}
}

static enum sway_input_idle_source idle_source_from_device(
		struct wlr_input_device *device) {
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		return IDLE_SOURCE_KEYBOARD;
	case WLR_INPUT_DEVICE_POINTER:
		return IDLE_SOURCE_POINTER;
	case WLR_INPUT_DEVICE_TOUCH:
		return IDLE_SOURCE_TOUCH;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return IDLE_SOURCE_TABLET_TOOL;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		return IDLE_SOURCE_TABLET_PAD;
	case WLR_INPUT_DEVICE_SWITCH:
		return IDLE_SOURCE_SWITCH;
	}

	abort();
}

void cursor_handle_activity_from_idle_source(struct sway_cursor *cursor,
		enum sway_input_idle_source idle_source) {
	wl_event_source_timer_update(
			cursor->hide_source, cursor_get_timeout(cursor));

	seat_idle_notify_activity(cursor->seat, idle_source);
	if (idle_source != IDLE_SOURCE_TOUCH) {
		cursor_unhide(cursor);
	}
}

void cursor_handle_activity_from_device(struct sway_cursor *cursor,
		struct wlr_input_device *device) {
	enum sway_input_idle_source idle_source = idle_source_from_device(device);
	cursor_handle_activity_from_idle_source(cursor, idle_source);
}

void cursor_unhide(struct sway_cursor *cursor) {
	if (!cursor->hidden) {
		return;
	}

	cursor->hidden = false;
	if (cursor->image_surface) {
		cursor_set_image_surface(cursor,
				cursor->image_surface,
				cursor->hotspot_x,
				cursor->hotspot_y,
				cursor->image_client);
	} else {
		const char *image = cursor->image;
		cursor->image = NULL;
		cursor_set_image(cursor, image, cursor->image_client);
	}
	cursor_rebase(cursor);
	wl_event_source_timer_update(cursor->hide_source, cursor_get_timeout(cursor));
}

static void pointer_motion(struct sway_cursor *cursor, uint32_t time_msec,
		struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel) {
	wlr_relative_pointer_manager_v1_send_relative_motion(
		server.relative_pointer_manager,
		cursor->seat->wlr_seat, (uint64_t)time_msec * 1000,
		dx, dy, dx_unaccel, dy_unaccel);

	// Only apply pointer constraints to real pointer input.
	if (cursor->active_constraint && device->type == WLR_INPUT_DEVICE_POINTER) {
		struct wlr_surface *surface = NULL;
		double sx, sy;
		node_at_coords(cursor->seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

		if (cursor->active_constraint->surface != surface) {
			return;
		}

		double sx_confined, sy_confined;
		if (!wlr_region_confine(&cursor->confine, sx, sy, sx + dx, sy + dy,
				&sx_confined, &sy_confined)) {
			return;
		}

		dx = sx_confined - sx;
		dy = sy_confined - sy;
	}

	wlr_cursor_move(cursor->cursor, device, dx, dy);

	seatop_pointer_motion(cursor->seat, time_msec);
}

static void handle_pointer_motion_relative(
		struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, motion);
	struct wlr_event_pointer_motion *e = data;
	cursor_handle_activity_from_device(cursor, e->device);

	pointer_motion(cursor, e->time_msec, e->device, e->delta_x, e->delta_y,
			e->unaccel_dx, e->unaccel_dy);
}

static void handle_pointer_motion_absolute(
		struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor =
		wl_container_of(listener, cursor, motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;
	cursor_handle_activity_from_device(cursor, event->device);

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->cursor, event->device,
			event->x, event->y, &lx, &ly);

	double dx = lx - cursor->cursor->x;
	double dy = ly - cursor->cursor->y;

	pointer_motion(cursor, event->time_msec, event->device, dx, dy, dx, dy);
}

void dispatch_cursor_button(struct sway_cursor *cursor,
		struct wlr_input_device *device, uint32_t time_msec, uint32_t button,
		enum wlr_button_state state) {
	if (time_msec == 0) {
		time_msec = get_current_time_msec();
	}

	seatop_button(cursor->seat, time_msec, device, button, state);
}

static void handle_pointer_button(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, button);
	struct wlr_event_pointer_button *event = data;

	if (event->state == WLR_BUTTON_PRESSED) {
		cursor->pressed_button_count++;
	} else {
		if (cursor->pressed_button_count > 0) {
			cursor->pressed_button_count--;
		} else {
			sway_log(SWAY_ERROR, "Pressed button count was wrong");
		}
	}

	cursor_handle_activity_from_device(cursor, event->device);
	dispatch_cursor_button(cursor, event->device,
			event->time_msec, event->button, event->state);
}

void dispatch_cursor_axis(struct sway_cursor *cursor,
		struct wlr_event_pointer_axis *event) {
	seatop_pointer_axis(cursor->seat, event);
}

static void handle_pointer_axis(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, axis);
	struct wlr_event_pointer_axis *event = data;
	cursor_handle_activity_from_device(cursor, event->device);
	dispatch_cursor_axis(cursor, event);
}

static void handle_pointer_frame(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, frame);
	wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
}

static void handle_touch_down(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, touch_down);
	struct wlr_event_touch_down *event = data;
	cursor_handle_activity_from_device(cursor, event->device);
	cursor_hide(cursor);

	struct sway_seat *seat = cursor->seat;
	struct wlr_seat *wlr_seat = seat->wlr_seat;
	struct wlr_surface *surface = NULL;

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->cursor, event->device,
			event->x, event->y, &lx, &ly);
	double sx, sy;
	struct sway_node *focused_node = node_at_coords(seat, lx, ly, &surface, &sx, &sy);

	seat->touch_id = event->touch_id;
	seat->touch_x = lx;
	seat->touch_y = ly;

	if (surface && wlr_surface_accepts_touch(wlr_seat, surface)) {
		if (seat_is_input_allowed(seat, surface)) {
			wlr_seat_touch_notify_down(wlr_seat, surface, event->time_msec,
					event->touch_id, sx, sy);

			if (focused_node) {
			    seat_set_focus(seat, focused_node);
			}
		}
	} else if (!cursor->simulating_pointer_from_touch &&
			(!surface || seat_is_input_allowed(seat, surface))) {
		// Fallback to cursor simulation.
		// The pointer_touch_id state is needed, so drags are not aborted when over
		// a surface supporting touch and multi touch events don't interfere.
		cursor->simulating_pointer_from_touch = true;
		cursor->pointer_touch_id = seat->touch_id;
		double dx, dy;
		dx = lx - cursor->cursor->x;
		dy = ly - cursor->cursor->y;
		pointer_motion(cursor, event->time_msec, event->device, dx, dy, dx, dy);
		dispatch_cursor_button(cursor, event->device, event->time_msec,
				BTN_LEFT, WLR_BUTTON_PRESSED);
	}
}

static void handle_touch_up(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, touch_up);
	struct wlr_event_touch_up *event = data;
	cursor_handle_activity_from_device(cursor, event->device);

	struct wlr_seat *wlr_seat = cursor->seat->wlr_seat;

	if (cursor->simulating_pointer_from_touch) {
		if (cursor->pointer_touch_id == cursor->seat->touch_id) {
			cursor->pointer_touch_up = true;
			dispatch_cursor_button(cursor, event->device, event->time_msec,
					BTN_LEFT, WLR_BUTTON_RELEASED);
		}
	} else {
		wlr_seat_touch_notify_up(wlr_seat, event->time_msec, event->touch_id);
	}
}

static void handle_touch_motion(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor =
		wl_container_of(listener, cursor, touch_motion);
	struct wlr_event_touch_motion *event = data;
	cursor_handle_activity_from_device(cursor, event->device);

	struct sway_seat *seat = cursor->seat;
	struct wlr_seat *wlr_seat = seat->wlr_seat;
	struct wlr_surface *surface = NULL;

	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(cursor->cursor, event->device,
			event->x, event->y, &lx, &ly);
	double sx, sy;
	node_at_coords(cursor->seat, lx, ly, &surface, &sx, &sy);

	if (seat->touch_id == event->touch_id) {
		seat->touch_x = lx;
		seat->touch_y = ly;

		struct sway_drag_icon *drag_icon;
		wl_list_for_each(drag_icon, &root->drag_icons, link) {
			if (drag_icon->seat == seat) {
				drag_icon_update_position(drag_icon);
			}
		}
	}

	if (cursor->simulating_pointer_from_touch) {
		if (seat->touch_id == cursor->pointer_touch_id) {
			double dx, dy;
			dx = lx - cursor->cursor->x;
			dy = ly - cursor->cursor->y;
			pointer_motion(cursor, event->time_msec, event->device, dx, dy, dx, dy);
		}
	} else if (surface) {
		wlr_seat_touch_notify_motion(wlr_seat, event->time_msec,
			event->touch_id, sx, sy);
	}
}

static void handle_touch_frame(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor =
		wl_container_of(listener, cursor, touch_frame);

	struct wlr_seat *wlr_seat = cursor->seat->wlr_seat;

	if (cursor->simulating_pointer_from_touch) {
		wlr_seat_pointer_notify_frame(wlr_seat);

		if (cursor->pointer_touch_up) {
			cursor->pointer_touch_up = false;
			cursor->simulating_pointer_from_touch = false;
		}
	} else {
		wlr_seat_touch_notify_frame(wlr_seat);
	}
}

static double apply_mapping_from_coord(double low, double high, double value) {
	if (isnan(value)) {
		return value;
	}

	return (value - low) / (high - low);
}

static void apply_mapping_from_region(struct wlr_input_device *device,
		struct input_config_mapped_from_region *region, double *x, double *y) {
	double x1 = region->x1, x2 = region->x2;
	double y1 = region->y1, y2 = region->y2;

	if (region->mm) {
		if (device->width_mm == 0 || device->height_mm == 0) {
			return;
		}
		x1 /= device->width_mm;
		x2 /= device->width_mm;
		y1 /= device->height_mm;
		y2 /= device->height_mm;
	}

	*x = apply_mapping_from_coord(x1, x2, *x);
	*y = apply_mapping_from_coord(y1, y2, *y);
}

static void handle_tablet_tool_position(struct sway_cursor *cursor,
		struct sway_tablet_tool *tool,
		bool change_x, bool change_y,
		double x, double y, double dx, double dy,
		int32_t time_msec) {

	if (!change_x && !change_y) {
		return;
	}

	struct sway_tablet *tablet = tool->tablet;
	struct sway_input_device *input_device = tablet->seat_device->input_device;
	struct input_config *ic = input_device_get_config(input_device);
	if (ic != NULL && ic->mapped_from_region != NULL) {
		apply_mapping_from_region(input_device->wlr_device,
			ic->mapped_from_region, &x, &y);
	}

	switch (tool->mode) {
	case SWAY_TABLET_TOOL_MODE_ABSOLUTE:
		wlr_cursor_warp_absolute(cursor->cursor, input_device->wlr_device,
			change_x ? x : NAN, change_y ? y : NAN);
		break;
	case SWAY_TABLET_TOOL_MODE_RELATIVE:
		wlr_cursor_move(cursor->cursor, input_device->wlr_device, dx, dy);
		break;
	}

	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct sway_seat *seat = cursor->seat;
	node_at_coords(seat, cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

	// The logic for whether we should send a tablet event or an emulated pointer
	// event is tricky. It comes down to:
	// * If we began a drag on a non-tablet surface (simulating_pointer_from_tool_tip),
	//   then we should continue sending emulated pointer events regardless of
	//   whether the surface currently under us accepts tablet or not.
	// * Otherwise, if we are over a surface that accepts tablet, then we should
	//   send tablet events.
	// * If we began a drag over a tablet surface, we should continue sending
	//   tablet events until the drag is released, even if we are now over a
	//   non-tablet surface.
	if (!cursor->simulating_pointer_from_tool_tip &&
			((surface && wlr_surface_accepts_tablet_v2(tablet->tablet_v2, surface)) ||
				wlr_tablet_tool_v2_has_implicit_grab(tool->tablet_v2_tool))) {
		seatop_tablet_tool_motion(seat, tool, time_msec);
	} else {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->tablet_v2_tool);
		pointer_motion(cursor, time_msec, input_device->wlr_device, dx, dy, dx, dy);
	}
}

static void handle_tool_axis(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, tool_axis);
	struct wlr_event_tablet_tool_axis *event = data;
	cursor_handle_activity_from_device(cursor, event->device);

	struct sway_tablet_tool *sway_tool = event->tool->data;
	if (!sway_tool) {
		sway_log(SWAY_DEBUG, "tool axis before proximity");
		return;
	}

	handle_tablet_tool_position(cursor, sway_tool,
		event->updated_axes & WLR_TABLET_TOOL_AXIS_X,
		event->updated_axes & WLR_TABLET_TOOL_AXIS_Y,
		event->x, event->y, event->dx, event->dy, event->time_msec);

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
		wlr_tablet_v2_tablet_tool_notify_pressure(
			sway_tool->tablet_v2_tool, event->pressure);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE) {
		wlr_tablet_v2_tablet_tool_notify_distance(
			sway_tool->tablet_v2_tool, event->distance);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X) {
		sway_tool->tilt_x = event->tilt_x;
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y) {
		sway_tool->tilt_y = event->tilt_y;
	}

	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y)) {
		wlr_tablet_v2_tablet_tool_notify_tilt(
			sway_tool->tablet_v2_tool,
			sway_tool->tilt_x, sway_tool->tilt_y);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION) {
		wlr_tablet_v2_tablet_tool_notify_rotation(
			sway_tool->tablet_v2_tool, event->rotation);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER) {
		wlr_tablet_v2_tablet_tool_notify_slider(
			sway_tool->tablet_v2_tool, event->slider);
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL) {
		wlr_tablet_v2_tablet_tool_notify_wheel(
			sway_tool->tablet_v2_tool, event->wheel_delta, 0);
	}
}

static void handle_tool_tip(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, tool_tip);
	struct wlr_event_tablet_tool_tip *event = data;
	cursor_handle_activity_from_device(cursor, event->device);

	struct sway_tablet_tool *sway_tool = event->tool->data;
	struct wlr_tablet_v2_tablet *tablet_v2 = sway_tool->tablet->tablet_v2;
	struct sway_seat *seat = cursor->seat;


	double sx, sy;
	struct wlr_surface *surface = NULL;
	node_at_coords(seat, cursor->cursor->x, cursor->cursor->y,
		&surface, &sx, &sy);

	if (cursor->simulating_pointer_from_tool_tip &&
			event->state == WLR_TABLET_TOOL_TIP_UP) {
		cursor->simulating_pointer_from_tool_tip = false;
		dispatch_cursor_button(cursor, event->device, event->time_msec,
			BTN_LEFT, WLR_BUTTON_RELEASED);
		wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
	} else if (!surface || !wlr_surface_accepts_tablet_v2(tablet_v2, surface)) {
		// If we started holding the tool tip down on a surface that accepts
		// tablet v2, we should notify that surface if it gets released over a
		// surface that doesn't support v2.
		if (event->state == WLR_TABLET_TOOL_TIP_UP) {
			seatop_tablet_tool_tip(seat, sway_tool, event->time_msec,
				WLR_TABLET_TOOL_TIP_UP);
		} else {
			cursor->simulating_pointer_from_tool_tip = true;
			dispatch_cursor_button(cursor, event->device, event->time_msec,
				BTN_LEFT, WLR_BUTTON_PRESSED);
			wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
		}
	} else {
		seatop_tablet_tool_tip(seat, sway_tool, event->time_msec, event->state);
	}
}

static struct sway_tablet *get_tablet_for_device(struct sway_cursor *cursor,
		struct wlr_input_device *device) {
	struct sway_tablet *tablet;
	wl_list_for_each(tablet, &cursor->tablets, link) {
		if (tablet->seat_device->input_device->wlr_device == device) {
			return tablet;
		}
	}
	return NULL;
}

static void handle_tool_proximity(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor =
		wl_container_of(listener, cursor, tool_proximity);
	struct wlr_event_tablet_tool_proximity *event = data;
	cursor_handle_activity_from_device(cursor, event->device);

	struct wlr_tablet_tool *tool = event->tool;
	if (!tool->data) {
		struct sway_tablet *tablet = get_tablet_for_device(cursor, event->device);
		if (!tablet) {
			sway_log(SWAY_ERROR, "no tablet for tablet tool");
			return;
		}
		sway_tablet_tool_configure(tablet, tool);
	}

	struct sway_tablet_tool *sway_tool = tool->data;
	if (!sway_tool) {
		sway_log(SWAY_ERROR, "tablet tool not initialized");
		return;
	}

	if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(sway_tool->tablet_v2_tool);
		return;
	}

	handle_tablet_tool_position(cursor, sway_tool, true, true, event->x, event->y,
		0, 0, event->time_msec);
}

static void handle_tool_button(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(listener, cursor, tool_button);
	struct wlr_event_tablet_tool_button *event = data;
	cursor_handle_activity_from_device(cursor, event->device);

	struct sway_tablet_tool *sway_tool = event->tool->data;
	if (!sway_tool) {
		sway_log(SWAY_DEBUG, "tool button before proximity");
		return;
	}
	struct wlr_tablet_v2_tablet *tablet_v2 = sway_tool->tablet->tablet_v2;

	double sx, sy;
	struct wlr_surface *surface = NULL;

	node_at_coords(cursor->seat, cursor->cursor->x, cursor->cursor->y,
		&surface, &sx, &sy);

	if (!surface || !wlr_surface_accepts_tablet_v2(tablet_v2, surface)) {
		// TODO: the user may want to configure which tool buttons are mapped to
		// which simulated pointer buttons
		switch (event->state) {
		case WLR_BUTTON_PRESSED:
			if (cursor->tool_buttons == 0) {
				dispatch_cursor_button(cursor, event->device,
						event->time_msec, BTN_RIGHT, event->state);
			}
			cursor->tool_buttons++;
			break;
		case WLR_BUTTON_RELEASED:
			if (cursor->tool_buttons == 1) {
				dispatch_cursor_button(cursor, event->device,
						event->time_msec, BTN_RIGHT, event->state);
			}
			cursor->tool_buttons--;
			break;
		}
		wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
		return;
	}

	wlr_tablet_v2_tablet_tool_notify_button(sway_tool->tablet_v2_tool,
		event->button, (enum zwp_tablet_pad_v2_button_state)event->state);
}

static void check_constraint_region(struct sway_cursor *cursor) {
	struct wlr_pointer_constraint_v1 *constraint = cursor->active_constraint;
	pixman_region32_t *region = &constraint->region;
	struct sway_view *view = view_from_wlr_surface(constraint->surface);
	if (cursor->active_confine_requires_warp && view) {
		cursor->active_confine_requires_warp = false;

		struct sway_container *con = view->container;

		double sx = cursor->cursor->x - con->pending.content_x + view->geometry.x;
		double sy = cursor->cursor->y - con->pending.content_y + view->geometry.y;

		if (!pixman_region32_contains_point(region,
				floor(sx), floor(sy), NULL)) {
			int nboxes;
			pixman_box32_t *boxes = pixman_region32_rectangles(region, &nboxes);
			if (nboxes > 0) {
				double sx = (boxes[0].x1 + boxes[0].x2) / 2.;
				double sy = (boxes[0].y1 + boxes[0].y2) / 2.;

				wlr_cursor_warp_closest(cursor->cursor, NULL,
					sx + con->pending.content_x - view->geometry.x,
					sy + con->pending.content_y - view->geometry.y);

				cursor_rebase(cursor);
			}
		}
	}

	// A locked pointer will result in an empty region, thus disallowing all movement
	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED) {
		pixman_region32_copy(&cursor->confine, region);
	} else {
		pixman_region32_clear(&cursor->confine);
	}
}

static void handle_constraint_commit(struct wl_listener *listener,
		void *data) {
	struct sway_cursor *cursor =
		wl_container_of(listener, cursor, constraint_commit);
	struct wlr_pointer_constraint_v1 *constraint = cursor->active_constraint;
	assert(constraint->surface == data);

	check_constraint_region(cursor);
}

static void handle_pointer_constraint_set_region(struct wl_listener *listener,
		void *data) {
	struct sway_pointer_constraint *sway_constraint =
		wl_container_of(listener, sway_constraint, set_region);
	struct sway_cursor *cursor = sway_constraint->cursor;

	cursor->active_confine_requires_warp = true;
}

static void handle_request_pointer_set_cursor(struct wl_listener *listener,
		void *data) {
	struct sway_cursor *cursor =
		wl_container_of(listener, cursor, request_set_cursor);
	if (!seatop_allows_set_cursor(cursor->seat)) {
		return;
	}
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	struct wl_client *focused_client = NULL;
	struct wlr_surface *focused_surface =
		cursor->seat->wlr_seat->pointer_state.focused_surface;
	if (focused_surface != NULL) {
		focused_client = wl_resource_get_client(focused_surface->resource);
	}

	// TODO: check cursor mode
	if (focused_client == NULL ||
			event->seat_client->client != focused_client) {
		sway_log(SWAY_DEBUG, "denying request to set cursor from unfocused client");
		return;
	}

	cursor_set_image_surface(cursor, event->surface, event->hotspot_x,
			event->hotspot_y, focused_client);
}

static void handle_pointer_pinch_begin(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(
			listener, cursor, pinch_begin);
	struct wlr_event_pointer_pinch_begin *event = data;
	wlr_pointer_gestures_v1_send_pinch_begin(
			cursor->pointer_gestures, cursor->seat->wlr_seat,
			event->time_msec, event->fingers);
}

static void handle_pointer_pinch_update(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(
			listener, cursor, pinch_update);
	struct wlr_event_pointer_pinch_update *event = data;
	wlr_pointer_gestures_v1_send_pinch_update(
			cursor->pointer_gestures, cursor->seat->wlr_seat,
			event->time_msec, event->dx, event->dy,
			event->scale, event->rotation);
}

static void handle_pointer_pinch_end(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(
			listener, cursor, pinch_end);
	struct wlr_event_pointer_pinch_end *event = data;
	wlr_pointer_gestures_v1_send_pinch_end(
			cursor->pointer_gestures, cursor->seat->wlr_seat,
			event->time_msec, event->cancelled);
}

static void handle_pointer_swipe_begin(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(
			listener, cursor, swipe_begin);
	struct wlr_event_pointer_swipe_begin *event = data;
	wlr_pointer_gestures_v1_send_swipe_begin(
			cursor->pointer_gestures, cursor->seat->wlr_seat,
			event->time_msec, event->fingers);
}

static void handle_pointer_swipe_update(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(
			listener, cursor, swipe_update);
	struct wlr_event_pointer_swipe_update *event = data;
	wlr_pointer_gestures_v1_send_swipe_update(
			cursor->pointer_gestures, cursor->seat->wlr_seat,
			event->time_msec, event->dx, event->dy);
}

static void handle_pointer_swipe_end(struct wl_listener *listener, void *data) {
	struct sway_cursor *cursor = wl_container_of(
			listener, cursor, swipe_end);
	struct wlr_event_pointer_swipe_end *event = data;
	wlr_pointer_gestures_v1_send_swipe_end(
			cursor->pointer_gestures, cursor->seat->wlr_seat,
			event->time_msec, event->cancelled);
}

static void handle_image_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_cursor *cursor =
		wl_container_of(listener, cursor, image_surface_destroy);
	cursor_set_image(cursor, NULL, cursor->image_client);
	cursor_rebase(cursor);
}

static void set_image_surface(struct sway_cursor *cursor,
		struct wlr_surface *surface) {
	wl_list_remove(&cursor->image_surface_destroy.link);
	cursor->image_surface = surface;
	if (surface) {
		wl_signal_add(&surface->events.destroy, &cursor->image_surface_destroy);
	} else {
		wl_list_init(&cursor->image_surface_destroy.link);
	}
}

void cursor_set_image(struct sway_cursor *cursor, const char *image,
		struct wl_client *client) {
	if (!(cursor->seat->wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER)) {
		return;
	}

	const char *current_image = cursor->image;
	set_image_surface(cursor, NULL);
	cursor->image = image;
	cursor->hotspot_x = cursor->hotspot_y = 0;
	cursor->image_client = client;

	if (cursor->hidden) {
		return;
	}

	if (!image) {
		wlr_cursor_set_image(cursor->cursor, NULL, 0, 0, 0, 0, 0, 0);
	} else if (!current_image || strcmp(current_image, image) != 0) {
		wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager, image,
				cursor->cursor);
	}
}

void cursor_set_image_surface(struct sway_cursor *cursor,
		struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y,
		struct wl_client *client) {
	if (!(cursor->seat->wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER)) {
		return;
	}

	set_image_surface(cursor, surface);
	cursor->image = NULL;
	cursor->hotspot_x = hotspot_x;
	cursor->hotspot_y = hotspot_y;
	cursor->image_client = client;

	if (cursor->hidden) {
		return;
	}

	wlr_cursor_set_surface(cursor->cursor, surface, hotspot_x, hotspot_y);
}

void sway_cursor_destroy(struct sway_cursor *cursor) {
	if (!cursor) {
		return;
	}

	wl_event_source_remove(cursor->hide_source);

	wl_list_remove(&cursor->image_surface_destroy.link);
	wl_list_remove(&cursor->pinch_begin.link);
	wl_list_remove(&cursor->pinch_update.link);
	wl_list_remove(&cursor->pinch_end.link);
	wl_list_remove(&cursor->swipe_begin.link);
	wl_list_remove(&cursor->swipe_update.link);
	wl_list_remove(&cursor->swipe_end.link);
	wl_list_remove(&cursor->motion.link);
	wl_list_remove(&cursor->motion_absolute.link);
	wl_list_remove(&cursor->button.link);
	wl_list_remove(&cursor->axis.link);
	wl_list_remove(&cursor->frame.link);
	wl_list_remove(&cursor->touch_down.link);
	wl_list_remove(&cursor->touch_up.link);
	wl_list_remove(&cursor->touch_motion.link);
	wl_list_remove(&cursor->touch_frame.link);
	wl_list_remove(&cursor->tool_axis.link);
	wl_list_remove(&cursor->tool_tip.link);
	wl_list_remove(&cursor->tool_button.link);
	wl_list_remove(&cursor->request_set_cursor.link);

	wlr_xcursor_manager_destroy(cursor->xcursor_manager);
	wlr_cursor_destroy(cursor->cursor);
	free(cursor);
}

struct sway_cursor *sway_cursor_create(struct sway_seat *seat) {
	struct sway_cursor *cursor = calloc(1, sizeof(struct sway_cursor));
	if (!sway_assert(cursor, "could not allocate sway cursor")) {
		return NULL;
	}

	struct wlr_cursor *wlr_cursor = wlr_cursor_create();
	if (!sway_assert(wlr_cursor, "could not allocate wlr cursor")) {
		free(cursor);
		return NULL;
	}

	cursor->previous.x = wlr_cursor->x;
	cursor->previous.y = wlr_cursor->y;

	cursor->seat = seat;
	wlr_cursor_attach_output_layout(wlr_cursor, root->output_layout);

	cursor->hide_source = wl_event_loop_add_timer(server.wl_event_loop,
			hide_notify, cursor);

	wl_list_init(&cursor->image_surface_destroy.link);
	cursor->image_surface_destroy.notify = handle_image_surface_destroy;

	cursor->pointer_gestures = wlr_pointer_gestures_v1_create(server.wl_display);
	cursor->pinch_begin.notify = handle_pointer_pinch_begin;
	wl_signal_add(&wlr_cursor->events.pinch_begin, &cursor->pinch_begin);
	cursor->pinch_update.notify = handle_pointer_pinch_update;
	wl_signal_add(&wlr_cursor->events.pinch_update, &cursor->pinch_update);
	cursor->pinch_end.notify = handle_pointer_pinch_end;
	wl_signal_add(&wlr_cursor->events.pinch_end, &cursor->pinch_end);
	cursor->swipe_begin.notify = handle_pointer_swipe_begin;
	wl_signal_add(&wlr_cursor->events.swipe_begin, &cursor->swipe_begin);
	cursor->swipe_update.notify = handle_pointer_swipe_update;
	wl_signal_add(&wlr_cursor->events.swipe_update, &cursor->swipe_update);
	cursor->swipe_end.notify = handle_pointer_swipe_end;
	wl_signal_add(&wlr_cursor->events.swipe_end, &cursor->swipe_end);

	// input events
	wl_signal_add(&wlr_cursor->events.motion, &cursor->motion);
	cursor->motion.notify = handle_pointer_motion_relative;

	wl_signal_add(&wlr_cursor->events.motion_absolute,
		&cursor->motion_absolute);
	cursor->motion_absolute.notify = handle_pointer_motion_absolute;

	wl_signal_add(&wlr_cursor->events.button, &cursor->button);
	cursor->button.notify = handle_pointer_button;

	wl_signal_add(&wlr_cursor->events.axis, &cursor->axis);
	cursor->axis.notify = handle_pointer_axis;

	wl_signal_add(&wlr_cursor->events.frame, &cursor->frame);
	cursor->frame.notify = handle_pointer_frame;

	wl_signal_add(&wlr_cursor->events.touch_down, &cursor->touch_down);
	cursor->touch_down.notify = handle_touch_down;

	wl_signal_add(&wlr_cursor->events.touch_up, &cursor->touch_up);
	cursor->touch_up.notify = handle_touch_up;

	wl_signal_add(&wlr_cursor->events.touch_motion,
		&cursor->touch_motion);
	cursor->touch_motion.notify = handle_touch_motion;

	wl_signal_add(&wlr_cursor->events.touch_frame, &cursor->touch_frame);
	cursor->touch_frame.notify = handle_touch_frame;

	wl_signal_add(&wlr_cursor->events.tablet_tool_axis,
		&cursor->tool_axis);
	cursor->tool_axis.notify = handle_tool_axis;

	wl_signal_add(&wlr_cursor->events.tablet_tool_tip, &cursor->tool_tip);
	cursor->tool_tip.notify = handle_tool_tip;

	wl_signal_add(&wlr_cursor->events.tablet_tool_proximity, &cursor->tool_proximity);
	cursor->tool_proximity.notify = handle_tool_proximity;

	wl_signal_add(&wlr_cursor->events.tablet_tool_button, &cursor->tool_button);
	cursor->tool_button.notify = handle_tool_button;

	wl_signal_add(&seat->wlr_seat->events.request_set_cursor,
			&cursor->request_set_cursor);
	cursor->request_set_cursor.notify = handle_request_pointer_set_cursor;

	wl_list_init(&cursor->constraint_commit.link);
	wl_list_init(&cursor->tablets);
	wl_list_init(&cursor->tablet_pads);

	cursor->cursor = wlr_cursor;

	return cursor;
}

/**
 * Warps the cursor to the middle of the container argument.
 * Does nothing if the cursor is already inside the container and `force` is
 * false. If container is NULL, returns without doing anything.
 */
void cursor_warp_to_container(struct sway_cursor *cursor,
		struct sway_container *container, bool force) {
	if (!container) {
		return;
	}

	struct wlr_box box;
	container_get_box(container, &box);
	if (!force && wlr_box_contains_point(&box, cursor->cursor->x,
			cursor->cursor->y)) {
		return;
	}

	double x = container->pending.x + container->pending.width / 2.0;
	double y = container->pending.y + container->pending.height / 2.0;

	wlr_cursor_warp(cursor->cursor, NULL, x, y);
	cursor_unhide(cursor);
}

/**
 * Warps the cursor to the middle of the workspace argument.
 * If workspace is NULL, returns without doing anything.
 */
void cursor_warp_to_workspace(struct sway_cursor *cursor,
		struct sway_workspace *workspace) {
	if (!workspace) {
		return;
	}

	double x = workspace->x + workspace->width / 2.0;
	double y = workspace->y + workspace->height / 2.0;

	wlr_cursor_warp(cursor->cursor, NULL, x, y);
	cursor_unhide(cursor);
}

uint32_t get_mouse_bindsym(const char *name, char **error) {
	if (strncasecmp(name, "button", strlen("button")) == 0) {
		// Map to x11 mouse buttons
		int number = name[strlen("button")] - '0';
		if (number < 1 || number > 9 || strlen(name) > strlen("button0")) {
			*error = strdup("Only buttons 1-9 are supported. For other mouse "
					"buttons, use the name of the event code.");
			return 0;
		}
		static const uint32_t buttons[] = {BTN_LEFT, BTN_MIDDLE, BTN_RIGHT,
			SWAY_SCROLL_UP, SWAY_SCROLL_DOWN, SWAY_SCROLL_LEFT,
			SWAY_SCROLL_RIGHT, BTN_SIDE, BTN_EXTRA};
		return buttons[number - 1];
	} else if (strncmp(name, "BTN_", strlen("BTN_")) == 0) {
		// Get event code from name
		int code = libevdev_event_code_from_name(EV_KEY, name);
		if (code == -1) {
			size_t len = snprintf(NULL, 0, "Unknown event %s", name) + 1;
			*error = malloc(len);
			if (*error) {
				snprintf(*error, len, "Unknown event %s", name);
			}
			return 0;
		}
		return code;
	}
	return 0;
}

uint32_t get_mouse_bindcode(const char *name, char **error) {
	// Validate event code
	errno = 0;
	char *endptr;
	int code = strtol(name, &endptr, 10);
	if (endptr == name && code <= 0) {
		*error = strdup("Button event code must be a positive integer.");
		return 0;
	} else if (errno == ERANGE) {
		*error = strdup("Button event code out of range.");
		return 0;
	}
	const char *event = libevdev_event_code_get_name(EV_KEY, code);
	if (!event || strncmp(event, "BTN_", strlen("BTN_")) != 0) {
		size_t len = snprintf(NULL, 0, "Event code %d (%s) is not a button",
				code, event ? event : "(null)") + 1;
		*error = malloc(len);
		if (*error) {
			snprintf(*error, len, "Event code %d (%s) is not a button",
					code, event ? event : "(null)");
		}
		return 0;
	}
	return code;
}

uint32_t get_mouse_button(const char *name, char **error) {
	uint32_t button = get_mouse_bindsym(name, error);
	if (!button && !*error) {
		button = get_mouse_bindcode(name, error);
	}
	return button;
}

const char *get_mouse_button_name(uint32_t button) {
	const char *name = libevdev_event_code_get_name(EV_KEY, button);
	if (!name) {
		if (button == SWAY_SCROLL_UP) {
			name = "SWAY_SCROLL_UP";
		} else if (button == SWAY_SCROLL_DOWN) {
			name = "SWAY_SCROLL_DOWN";
		} else if (button == SWAY_SCROLL_LEFT) {
			name = "SWAY_SCROLL_LEFT";
		} else if (button == SWAY_SCROLL_RIGHT) {
			name = "SWAY_SCROLL_RIGHT";
		}
	}
	return name;
}

static void warp_to_constraint_cursor_hint(struct sway_cursor *cursor) {
	struct wlr_pointer_constraint_v1 *constraint = cursor->active_constraint;

	if (constraint->current.committed &
			WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
		double sx = constraint->current.cursor_hint.x;
		double sy = constraint->current.cursor_hint.y;

		struct sway_view *view = view_from_wlr_surface(constraint->surface);
		struct sway_container *con = view->container;

		double lx = sx + con->pending.content_x - view->geometry.x;
		double ly = sy + con->pending.content_y - view->geometry.y;

		wlr_cursor_warp(cursor->cursor, NULL, lx, ly);

		// Warp the pointer as well, so that on the next pointer rebase we don't
		// send an unexpected synthetic motion event to clients.
		wlr_seat_pointer_warp(constraint->seat, sx, sy);
	}
}

void handle_constraint_destroy(struct wl_listener *listener, void *data) {
	struct sway_pointer_constraint *sway_constraint =
		wl_container_of(listener, sway_constraint, destroy);
	struct wlr_pointer_constraint_v1 *constraint = data;
	struct sway_cursor *cursor = sway_constraint->cursor;

	wl_list_remove(&sway_constraint->set_region.link);
	wl_list_remove(&sway_constraint->destroy.link);

	if (cursor->active_constraint == constraint) {
		warp_to_constraint_cursor_hint(cursor);

		if (cursor->constraint_commit.link.next != NULL) {
			wl_list_remove(&cursor->constraint_commit.link);
		}
		wl_list_init(&cursor->constraint_commit.link);
		cursor->active_constraint = NULL;
	}

	free(sway_constraint);
}

void handle_pointer_constraint(struct wl_listener *listener, void *data) {
	struct wlr_pointer_constraint_v1 *constraint = data;
	struct sway_seat *seat = constraint->seat->data;

	struct sway_pointer_constraint *sway_constraint =
		calloc(1, sizeof(struct sway_pointer_constraint));
	sway_constraint->cursor = seat->cursor;
	sway_constraint->constraint = constraint;

	sway_constraint->set_region.notify = handle_pointer_constraint_set_region;
	wl_signal_add(&constraint->events.set_region, &sway_constraint->set_region);

	sway_constraint->destroy.notify = handle_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &sway_constraint->destroy);

	struct sway_node *focus = seat_get_focus(seat);
	if (focus && focus->type == N_CONTAINER && focus->sway_container->view) {
		struct wlr_surface *surface = focus->sway_container->view->surface;
		if (surface == constraint->surface) {
			sway_cursor_constrain(seat->cursor, constraint);
		}
	}
}

void sway_cursor_constrain(struct sway_cursor *cursor,
		struct wlr_pointer_constraint_v1 *constraint) {
	struct seat_config *config = seat_get_config(cursor->seat);
	if (!config) {
		config = seat_get_config_by_name("*");
	}

	if (!config || config->allow_constrain == CONSTRAIN_DISABLE) {
		return;
	}

	if (cursor->active_constraint == constraint) {
		return;
	}

	wl_list_remove(&cursor->constraint_commit.link);
	if (cursor->active_constraint) {
		if (constraint == NULL) {
			warp_to_constraint_cursor_hint(cursor);
		}
		wlr_pointer_constraint_v1_send_deactivated(
			cursor->active_constraint);
	}

	cursor->active_constraint = constraint;

	if (constraint == NULL) {
		wl_list_init(&cursor->constraint_commit.link);
		return;
	}

	cursor->active_confine_requires_warp = true;

	// FIXME: Big hack, stolen from wlr_pointer_constraints_v1.c:121.
	// This is necessary because the focus may be set before the surface
	// has finished committing, which means that warping won't work properly,
	// since this code will be run *after* the focus has been set.
	// That is why we duplicate the code here.
	if (pixman_region32_not_empty(&constraint->current.region)) {
		pixman_region32_intersect(&constraint->region,
			&constraint->surface->input_region, &constraint->current.region);
	} else {
		pixman_region32_copy(&constraint->region,
			&constraint->surface->input_region);
	}

	check_constraint_region(cursor);

	wlr_pointer_constraint_v1_send_activated(constraint);

	cursor->constraint_commit.notify = handle_constraint_commit;
	wl_signal_add(&constraint->surface->events.commit,
		&cursor->constraint_commit);
}
