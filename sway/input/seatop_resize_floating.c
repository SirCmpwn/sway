#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/tree/container.h"

struct seatop_resize_floating_event {
	struct sway_container *con;
	enum wlr_edges edge;
	bool preserve_ratio;
	double ref_lx, ref_ly;         // cursor's x/y at start of op
	double ref_width, ref_height;  // container's size at start of op
	double ref_con_lx, ref_con_ly; // container's x/y at start of op
};

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	struct seatop_resize_floating_event *e = seat->seatop_data;
	struct sway_container *con = e->con;

	if (seat->cursor->pressed_button_count == 0) {
		container_set_resizing(con, false);
		arrange_container(con); // Send configure w/o resizing hint
		transaction_commit_dirty();
		seatop_begin_default(seat);
	}
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_resize_floating_event *e = seat->seatop_data;
	struct sway_container *con = e->con;
	enum wlr_edges edge = e->edge;
	struct sway_cursor *cursor = seat->cursor;

	// The amount the mouse has moved since the start of the resize operation
	// Positive is down/right
	double mouse_move_x = cursor->cursor->x - e->ref_lx;
	double mouse_move_y = cursor->cursor->y - e->ref_ly;

	if (edge == WLR_EDGE_TOP || edge == WLR_EDGE_BOTTOM) {
		mouse_move_x = 0;
	}
	if (edge == WLR_EDGE_LEFT || edge == WLR_EDGE_RIGHT) {
		mouse_move_y = 0;
	}

	double grow_width = edge & WLR_EDGE_LEFT ? -mouse_move_x : mouse_move_x;
	double grow_height = edge & WLR_EDGE_TOP ? -mouse_move_y : mouse_move_y;

	if (e->preserve_ratio) {
		double x_multiplier = grow_width / e->ref_width;
		double y_multiplier = grow_height / e->ref_height;
		double max_multiplier = fmax(x_multiplier, y_multiplier);
		grow_width = e->ref_width * max_multiplier;
		grow_height = e->ref_height * max_multiplier;
	}

	struct sway_container_state *state = &con->current;
	double border_width = 0.0;
	if (con->current.border == B_NORMAL || con->current.border == B_PIXEL) {
		border_width = state->border_thickness * 2;
	}
	double border_height = 0.0;
	if (con->current.border == B_NORMAL) {
		border_height += container_titlebar_height();
		border_height += state->border_thickness;
	} else if (con->current.border == B_PIXEL) {
		border_height += state->border_thickness * 2;
	}

	// Determine new width/height, and accommodate for floating min/max values
	double width = e->ref_width + grow_width;
	double height = e->ref_height + grow_height;
	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	width = fmax(min_width + border_width, fmin(width, max_width));
	height = fmax(min_height + border_height, fmin(height, max_height));

	// Apply the view's min/max size
	if (con->view) {
		double view_min_width, view_max_width, view_min_height, view_max_height;
		view_get_constraints(con->view, &view_min_width, &view_max_width,
				&view_min_height, &view_max_height);
		width = fmax(view_min_width + border_width, fmin(width, view_max_width));
		height = fmax(view_min_height + border_height, fmin(height, view_max_height));

	}

	// Recalculate these, in case we hit a min/max limit
	grow_width = width - e->ref_width;
	grow_height = height - e->ref_height;

	// Determine grow x/y values - these are relative to the container's x/y at
	// the start of the resize operation.
	double grow_x = 0, grow_y = 0;
	if (edge & WLR_EDGE_LEFT) {
		grow_x = -grow_width;
	} else if (edge & WLR_EDGE_RIGHT) {
		grow_x = 0;
	} else {
		grow_x = -grow_width / 2;
	}
	if (edge & WLR_EDGE_TOP) {
		grow_y = -grow_height;
	} else if (edge & WLR_EDGE_BOTTOM) {
		grow_y = 0;
	} else {
		grow_y = -grow_height / 2;
	}

	// Determine the amounts we need to bump everything relative to the current
	// size.
	int relative_grow_width = width - con->pending.width;
	int relative_grow_height = height - con->pending.height;
	int relative_grow_x = (e->ref_con_lx + grow_x) - con->pending.x;
	int relative_grow_y = (e->ref_con_ly + grow_y) - con->pending.y;

	// Actually resize stuff
	con->pending.x += relative_grow_x;
	con->pending.y += relative_grow_y;
	con->pending.width += relative_grow_width;
	con->pending.height += relative_grow_height;

	con->pending.content_x += relative_grow_x;
	con->pending.content_y += relative_grow_y;
	con->pending.content_width += relative_grow_width;
	con->pending.content_height += relative_grow_height;

	arrange_container(con);
	transaction_commit_dirty();
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_resize_floating_event *e = seat->seatop_data;
	if (e->con == con) {
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.unref = handle_unref,
};

void seatop_begin_resize_floating(struct sway_seat *seat,
		struct sway_container *con, enum wlr_edges edge) {
	seatop_end(seat);

	struct seatop_resize_floating_event *e =
		calloc(1, sizeof(struct seatop_resize_floating_event));
	if (!e) {
		return;
	}
	e->con = con;

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
	e->preserve_ratio = keyboard &&
		(wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_SHIFT);

	e->edge = edge == WLR_EDGE_NONE ? WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT : edge;
	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;
	e->ref_con_lx = con->pending.x;
	e->ref_con_ly = con->pending.y;
	e->ref_width = con->pending.width;
	e->ref_height = con->pending.height;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_set_resizing(con, true);
	container_raise_floating(con);
	transaction_commit_dirty();

	const char *image = edge == WLR_EDGE_NONE ?
		"se-resize" : wlr_xcursor_get_resize_name(edge);
	cursor_set_image(seat->cursor, image, NULL);
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
