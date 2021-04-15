#define _POSIX_C_SOURCE 200809L
#include <float.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_tablet_v2.h>
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/tree/view.h"
#include "sway/desktop/transaction.h"
#include "log.h"

struct seatop_down_event {
	struct sway_container *con;
	double ref_lx, ref_ly;         // cursor's x/y at start of op
	double ref_con_lx, ref_con_ly; // container's x/y at start of op
};

static void handle_pointer_axis(struct sway_seat *seat,
		struct wlr_event_pointer_axis *event) {
	struct sway_input_device *input_device =
		event->device ? event->device->data : NULL;
	struct input_config *ic =
		input_device ? input_device_get_config(input_device) : NULL;
	float scroll_factor =
		(ic == NULL || ic->scroll_factor == FLT_MIN) ? 1.0f : ic->scroll_factor;

	wlr_seat_pointer_notify_axis(seat->wlr_seat, event->time_msec,
		event->orientation, scroll_factor * event->delta,
		round(scroll_factor * event->delta_discrete), event->source);
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	seat_pointer_notify_button(seat, time_msec, button, state);

	if (seat->cursor->pressed_button_count == 0) {
		seatop_begin_default(seat);
	}
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_down_event *e = seat->seatop_data;
	struct sway_container *con = e->con;
	if (seat_is_input_allowed(seat, con->view->surface)) {
		double moved_x = seat->cursor->cursor->x - e->ref_lx;
		double moved_y = seat->cursor->cursor->y - e->ref_ly;
		double sx = e->ref_con_lx + moved_x;
		double sy = e->ref_con_ly + moved_y;
		wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
	}
}

static void handle_tablet_tool_tip(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (state == WLR_TABLET_TOOL_TIP_UP) {
		wlr_tablet_v2_tablet_tool_notify_up(tool->tablet_v2_tool);
		seatop_begin_default(seat);
	}
}

static void handle_tablet_tool_motion(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec) {
	struct seatop_down_event *e = seat->seatop_data;
	struct sway_container *con = e->con;
	if (seat_is_input_allowed(seat, con->view->surface)) {
		double moved_x = seat->cursor->cursor->x - e->ref_lx;
		double moved_y = seat->cursor->cursor->y - e->ref_ly;
		double sx = e->ref_con_lx + moved_x;
		double sy = e->ref_con_ly + moved_y;
		wlr_tablet_v2_tablet_tool_notify_motion(tool->tablet_v2_tool, sx, sy);
	}
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_down_event *e = seat->seatop_data;
	if (e->con == con) {
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.pointer_axis = handle_pointer_axis,
	.tablet_tool_tip = handle_tablet_tool_tip,
	.tablet_tool_motion = handle_tablet_tool_motion,
	.unref = handle_unref,
	.allow_set_cursor = true,
};

void seatop_begin_down(struct sway_seat *seat, struct sway_container *con,
		uint32_t time_msec, int sx, int sy) {
	seatop_end(seat);

	struct seatop_down_event *e =
		calloc(1, sizeof(struct seatop_down_event));
	if (!e) {
		return;
	}
	e->con = con;
	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;
	e->ref_con_lx = sx;
	e->ref_con_ly = sy;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_raise_floating(con);
	transaction_commit_dirty();
}
