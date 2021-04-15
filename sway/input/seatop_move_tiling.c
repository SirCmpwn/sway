#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include "sway/desktop.h"
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/node.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

// Thickness of the dropzone when dragging to the edge of a layout container
#define DROP_LAYOUT_BORDER 30

struct seatop_move_tiling_event {
	struct sway_container *con;
	struct sway_node *target_node;
	enum wlr_edges target_edge;
	struct wlr_box drop_box;
	double ref_lx, ref_ly; // cursor's x/y at start of op
	bool threshold_reached;
};

static void handle_render(struct sway_seat *seat,
		struct sway_output *output, pixman_region32_t *damage) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (!e->threshold_reached) {
		return;
	}
	if (e->target_node && node_get_output(e->target_node) == output) {
		float color[4];
		memcpy(&color, config->border_colors.focused.indicator,
				sizeof(float) * 4);
		premultiply_alpha(color, 0.5);
		struct wlr_box box;
		memcpy(&box, &e->drop_box, sizeof(struct wlr_box));
		scale_box(&box, output->wlr_output->scale);
		render_rect(output, damage, &box, color);
	}
}

static void handle_motion_prethreshold(struct sway_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	double cx = seat->cursor->cursor->x;
	double cy = seat->cursor->cursor->y;
	double sx = e->ref_lx;
	double sy = e->ref_ly;

	// Get the scaled threshold for the output. Even if the operation goes
	// across multiple outputs of varying scales, just use the scale for the
	// output that the cursor is currently on for simplicity.
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
			root->output_layout, cx, cy);
	double output_scale = wlr_output ? wlr_output->scale : 1;
	double threshold = config->tiling_drag_threshold * output_scale;
	threshold *= threshold;

	// If the threshold has been exceeded, start the actual drag
	if ((cx - sx) * (cx - sx) + (cy - sy) * (cy - sy) > threshold) {
		e->threshold_reached = true;
		cursor_set_image(seat->cursor, "grab", NULL);
	}
}

static void resize_box(struct wlr_box *box, enum wlr_edges edge,
		int thickness) {
	switch (edge) {
	case WLR_EDGE_TOP:
		box->height = thickness;
		break;
	case WLR_EDGE_LEFT:
		box->width = thickness;
		break;
	case WLR_EDGE_RIGHT:
		box->x = box->x + box->width - thickness;
		box->width = thickness;
		break;
	case WLR_EDGE_BOTTOM:
		box->y = box->y + box->height - thickness;
		box->height = thickness;
		break;
	case WLR_EDGE_NONE:
		box->x += thickness;
		box->y += thickness;
		box->width -= thickness * 2;
		box->height -= thickness * 2;
		break;
	}
}

static void handle_motion_postthreshold(struct sway_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct sway_cursor *cursor = seat->cursor;
	struct sway_node *node = node_at_coords(seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);
	// Damage the old location
	desktop_damage_box(&e->drop_box);

	if (!node) {
		// Eg. hovered over a layer surface such as swaybar
		e->target_node = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}

	if (node->type == N_WORKSPACE) {
		// Empty workspace
		e->target_node = node;
		e->target_edge = WLR_EDGE_NONE;
		workspace_get_box(node->sway_workspace, &e->drop_box);
		desktop_damage_box(&e->drop_box);
		return;
	}

	// Deny moving within own workspace if this is the only child
	struct sway_container *con = node->sway_container;
	if (workspace_num_tiling_views(e->con->pending.workspace) == 1 &&
			con->pending.workspace == e->con->pending.workspace) {
		e->target_node = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}

	// Traverse the ancestors, trying to find a layout container perpendicular
	// to the edge. Eg. close to the top or bottom of a horiz layout.
	while (con) {
		enum wlr_edges edge = WLR_EDGE_NONE;
		enum sway_container_layout layout = container_parent_layout(con);
		struct wlr_box parent;
		con->pending.parent ? container_get_box(con->pending.parent, &parent) :
			workspace_get_box(con->pending.workspace, &parent);
		if (layout == L_HORIZ || layout == L_TABBED) {
			if (cursor->cursor->y < parent.y + DROP_LAYOUT_BORDER) {
				edge = WLR_EDGE_TOP;
			} else if (cursor->cursor->y > parent.y + parent.height
					- DROP_LAYOUT_BORDER) {
				edge = WLR_EDGE_BOTTOM;
			}
		} else if (layout == L_VERT || layout == L_STACKED) {
			if (cursor->cursor->x < parent.x + DROP_LAYOUT_BORDER) {
				edge = WLR_EDGE_LEFT;
			} else if (cursor->cursor->x > parent.x + parent.width
					- DROP_LAYOUT_BORDER) {
				edge = WLR_EDGE_RIGHT;
			}
		}
		if (edge) {
			e->target_node = node_get_parent(&con->node);
			if (e->target_node == &e->con->node) {
				e->target_node = node_get_parent(e->target_node);
			}
			e->target_edge = edge;
			node_get_box(e->target_node, &e->drop_box);
			resize_box(&e->drop_box, edge, DROP_LAYOUT_BORDER);
			desktop_damage_box(&e->drop_box);
			return;
		}
		con = con->pending.parent;
	}

	// Use the hovered view - but we must be over the actual surface
	con = node->sway_container;
	if (!con->view || !con->view->surface || node == &e->con->node
			|| node_has_ancestor(node, &e->con->node)) {
		e->target_node = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}

	// Find the closest edge
	size_t thickness = fmin(con->pending.content_width, con->pending.content_height) * 0.3;
	size_t closest_dist = INT_MAX;
	size_t dist;
	e->target_edge = WLR_EDGE_NONE;
	if ((dist = cursor->cursor->y - con->pending.y) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_TOP;
	}
	if ((dist = cursor->cursor->x - con->pending.x) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_LEFT;
	}
	if ((dist = con->pending.x + con->pending.width - cursor->cursor->x) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_RIGHT;
	}
	if ((dist = con->pending.y + con->pending.height - cursor->cursor->y) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_BOTTOM;
	}

	if (closest_dist > thickness) {
		e->target_edge = WLR_EDGE_NONE;
	}

	e->target_node = node;
	e->drop_box.x = con->pending.content_x;
	e->drop_box.y = con->pending.content_y;
	e->drop_box.width = con->pending.content_width;
	e->drop_box.height = con->pending.content_height;
	resize_box(&e->drop_box, e->target_edge, thickness);
	desktop_damage_box(&e->drop_box);
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e->threshold_reached) {
		handle_motion_postthreshold(seat);
	} else {
		handle_motion_prethreshold(seat);
	}
	transaction_commit_dirty();
}

static bool is_parallel(enum sway_container_layout layout,
		enum wlr_edges edge) {
	bool layout_is_horiz = layout == L_HORIZ || layout == L_TABBED;
	bool edge_is_horiz = edge == WLR_EDGE_LEFT || edge == WLR_EDGE_RIGHT;
	return layout_is_horiz == edge_is_horiz;
}

static void finalize_move(struct sway_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;

	if (!e->target_node) {
		seatop_begin_default(seat);
		return;
	}

	struct sway_container *con = e->con;
	struct sway_container *old_parent = con->pending.parent;
	struct sway_workspace *old_ws = con->pending.workspace;
	struct sway_node *target_node = e->target_node;
	struct sway_workspace *new_ws = target_node->type == N_WORKSPACE ?
		target_node->sway_workspace : target_node->sway_container->pending.workspace;
	enum wlr_edges edge = e->target_edge;
	int after = edge != WLR_EDGE_TOP && edge != WLR_EDGE_LEFT;
	bool swap = edge == WLR_EDGE_NONE && target_node->type == N_CONTAINER;

	if (!swap) {
		container_detach(con);
	}

	// Moving container into empty workspace
	if (target_node->type == N_WORKSPACE && edge == WLR_EDGE_NONE) {
		con = workspace_add_tiling(new_ws, con);
	} else if (target_node->type == N_CONTAINER) {
		// Moving container before/after another
		struct sway_container *target = target_node->sway_container;
		if (swap) {
			container_swap(target_node->sway_container, con);
		} else {
			enum sway_container_layout layout = container_parent_layout(target);
			if (edge && !is_parallel(layout, edge)) {
				enum sway_container_layout new_layout = edge == WLR_EDGE_TOP ||
					edge == WLR_EDGE_BOTTOM ? L_VERT : L_HORIZ;
				container_split(target, new_layout);
			}
			container_add_sibling(target, con, after);
			ipc_event_window(con, "move");
		}
	} else {
		// Target is a workspace which requires splitting
		enum sway_container_layout new_layout = edge == WLR_EDGE_TOP ||
			edge == WLR_EDGE_BOTTOM ? L_VERT : L_HORIZ;
		workspace_split(new_ws, new_layout);
		workspace_insert_tiling(new_ws, con, after);
	}

	if (old_parent) {
		container_reap_empty(old_parent);
	}

	// This is a bit dirty, but we'll set the dimensions to that of a sibling.
	// I don't think there's any other way to make it consistent without
	// changing how we auto-size containers.
	list_t *siblings = container_get_siblings(con);
	if (siblings->length > 1) {
		int index = list_find(siblings, con);
		struct sway_container *sibling = index == 0 ?
			siblings->items[1] : siblings->items[index - 1];
		con->pending.width = sibling->pending.width;
		con->pending.height = sibling->pending.height;
		con->width_fraction = sibling->width_fraction;
		con->height_fraction = sibling->height_fraction;
	}

	arrange_workspace(old_ws);
	if (new_ws != old_ws) {
		arrange_workspace(new_ws);
	}

	transaction_commit_dirty();
	seatop_begin_default(seat);
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	if (seat->cursor->pressed_button_count == 0) {
		finalize_move(seat);
	}
}

static void handle_tablet_tool_tip(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (state == WLR_TABLET_TOOL_TIP_UP) {
		finalize_move(seat);
	}
}

static void handle_unref(struct sway_seat *seat, struct sway_container *con) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e->target_node == &con->node) { // Drop target
		e->target_node = NULL;
	}
	if (e->con == con) { // The container being moved
		seatop_begin_default(seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.tablet_tool_tip = handle_tablet_tool_tip,
	.unref = handle_unref,
	.render = handle_render,
};

void seatop_begin_move_tiling_threshold(struct sway_seat *seat,
		struct sway_container *con) {
	seatop_end(seat);

	struct seatop_move_tiling_event *e =
		calloc(1, sizeof(struct seatop_move_tiling_event));
	if (!e) {
		return;
	}
	e->con = con;
	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	container_raise_floating(con);
	transaction_commit_dirty();
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}

void seatop_begin_move_tiling(struct sway_seat *seat,
		struct sway_container *con) {
	seatop_begin_move_tiling_threshold(seat, con);
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e) {
		e->threshold_reached = true;
		cursor_set_image(seat->cursor, "grab", NULL);
	}
}
