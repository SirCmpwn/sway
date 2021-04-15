#define _POSIX_C_SOURCE 200809L
#include <float.h>
#include <libevdev/libevdev.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/input/tablet.h"
#include "sway/output.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "log.h"
#if HAVE_XWAYLAND
#include "sway/xwayland.h"
#endif

struct seatop_default_event {
	struct sway_node *previous_node;
	uint32_t pressed_buttons[SWAY_CURSOR_PRESSED_BUTTONS_CAP];
	size_t pressed_button_count;
};

/*-----------------------------------------\
 * Functions shared by multiple callbacks  /
 *---------------------------------------*/

/**
 * Determine if the edge of the given container is on the edge of the
 * workspace/output.
 */
static bool edge_is_external(struct sway_container *cont, enum wlr_edges edge) {
	enum sway_container_layout layout = L_NONE;
	switch (edge) {
	case WLR_EDGE_TOP:
	case WLR_EDGE_BOTTOM:
		layout = L_VERT;
		break;
	case WLR_EDGE_LEFT:
	case WLR_EDGE_RIGHT:
		layout = L_HORIZ;
		break;
	case WLR_EDGE_NONE:
		sway_assert(false, "Never reached");
		return false;
	}

	// Iterate the parents until we find one with the layout we want,
	// then check if the child has siblings between it and the edge.
	while (cont) {
		if (container_parent_layout(cont) == layout) {
			list_t *siblings = container_get_siblings(cont);
			int index = list_find(siblings, cont);
			if (index > 0 && (edge == WLR_EDGE_LEFT || edge == WLR_EDGE_TOP)) {
				return false;
			}
			if (index < siblings->length - 1 &&
					(edge == WLR_EDGE_RIGHT || edge == WLR_EDGE_BOTTOM)) {
				return false;
			}
		}
		cont = cont->pending.parent;
	}
	return true;
}

static enum wlr_edges find_edge(struct sway_container *cont,
		struct wlr_surface *surface, struct sway_cursor *cursor) {
	if (!cont->view || (surface && cont->view->surface != surface)) {
		return WLR_EDGE_NONE;
	}
	if (cont->pending.border == B_NONE || !cont->pending.border_thickness ||
			cont->pending.border == B_CSD) {
		return WLR_EDGE_NONE;
	}
	if (cont->pending.fullscreen_mode) {
		return WLR_EDGE_NONE;
	}

	enum wlr_edges edge = 0;
	if (cursor->cursor->x < cont->pending.x + cont->pending.border_thickness) {
		edge |= WLR_EDGE_LEFT;
	}
	if (cursor->cursor->y < cont->pending.y + cont->pending.border_thickness) {
		edge |= WLR_EDGE_TOP;
	}
	if (cursor->cursor->x >= cont->pending.x + cont->pending.width - cont->pending.border_thickness) {
		edge |= WLR_EDGE_RIGHT;
	}
	if (cursor->cursor->y >= cont->pending.y + cont->pending.height - cont->pending.border_thickness) {
		edge |= WLR_EDGE_BOTTOM;
	}

	return edge;
}

/**
 * If the cursor is over a _resizable_ edge, return the edge.
 * Edges that can't be resized are edges of the workspace.
 */
enum wlr_edges find_resize_edge(struct sway_container *cont,
		struct wlr_surface *surface, struct sway_cursor *cursor) {
	enum wlr_edges edge = find_edge(cont, surface, cursor);
	if (edge && !container_is_floating(cont) && edge_is_external(cont, edge)) {
		return WLR_EDGE_NONE;
	}
	return edge;
}

/**
 * Return the mouse binding which matches modifier, click location, release,
 * and pressed button state, otherwise return null.
 */
static struct sway_binding* get_active_mouse_binding(
		struct seatop_default_event *e, list_t *bindings, uint32_t modifiers,
		bool release, bool on_titlebar, bool on_border, bool on_content,
		bool on_workspace, const char *identifier) {
	uint32_t click_region =
			((on_titlebar || on_workspace) ? BINDING_TITLEBAR : 0) |
			((on_border || on_workspace) ? BINDING_BORDER : 0) |
			((on_content || on_workspace) ? BINDING_CONTENTS : 0);

	struct sway_binding *current = NULL;
	for (int i = 0; i < bindings->length; ++i) {
		struct sway_binding *binding = bindings->items[i];
		if (modifiers ^ binding->modifiers ||
				e->pressed_button_count != (size_t)binding->keys->length ||
				release != (binding->flags & BINDING_RELEASE) ||
				!(click_region & binding->flags) ||
				(on_workspace &&
				 (click_region & binding->flags) != click_region) ||
				(strcmp(binding->input, identifier) != 0 &&
				 strcmp(binding->input, "*") != 0)) {
			continue;
		}

		bool match = true;
		for (size_t j = 0; j < e->pressed_button_count; j++) {
			uint32_t key = *(uint32_t *)binding->keys->items[j];
			if (key != e->pressed_buttons[j]) {
				match = false;
				break;
			}
		}
		if (!match) {
			continue;
		}

		if (!current || strcmp(current->input, "*") == 0) {
			current = binding;
			if (strcmp(current->input, identifier) == 0) {
				// If a binding is found for the exact input, quit searching
				break;
			}
		}
	}
	return current;
}

/**
 * Remove a button (and duplicates) from the sorted list of currently pressed
 * buttons.
 */
static void state_erase_button(struct seatop_default_event *e,
		uint32_t button) {
	size_t j = 0;
	for (size_t i = 0; i < e->pressed_button_count; ++i) {
		if (i > j) {
			e->pressed_buttons[j] = e->pressed_buttons[i];
		}
		if (e->pressed_buttons[i] != button) {
			++j;
		}
	}
	while (e->pressed_button_count > j) {
		--e->pressed_button_count;
		e->pressed_buttons[e->pressed_button_count] = 0;
	}
}

/**
 * Add a button to the sorted list of currently pressed buttons, if there
 * is space.
 */
static void state_add_button(struct seatop_default_event *e, uint32_t button) {
	if (e->pressed_button_count >= SWAY_CURSOR_PRESSED_BUTTONS_CAP) {
		return;
	}
	size_t i = 0;
	while (i < e->pressed_button_count && e->pressed_buttons[i] < button) {
		++i;
	}
	size_t j = e->pressed_button_count;
	while (j > i) {
		e->pressed_buttons[j] = e->pressed_buttons[j - 1];
		--j;
	}
	e->pressed_buttons[i] = button;
	e->pressed_button_count++;
}

/*-------------------------------------------\
 * Functions used by handle_tablet_tool_tip  /
 *-----------------------------------------*/

static void handle_tablet_tool_tip(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (state == WLR_TABLET_TOOL_TIP_UP) {
		wlr_tablet_v2_tablet_tool_notify_up(tool->tablet_v2_tool);
		return;
	}

	struct sway_cursor *cursor = seat->cursor;
	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct sway_node *node = node_at_coords(seat,
		cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

	if (!sway_assert(surface,
			"Expected null-surface tablet input to route through pointer emulation")) {
		return;
	}

	struct sway_container *cont = node && node->type == N_CONTAINER ?
		node->sway_container : NULL;

	if (wlr_surface_is_layer_surface(surface)) {
		// Handle tapping a layer surface
		struct wlr_layer_surface_v1 *layer =
				wlr_layer_surface_v1_from_wlr_surface(surface);
		if (layer->current.keyboard_interactive) {
			seat_set_focus_layer(seat, layer);
			transaction_commit_dirty();
		}
	} else if (cont) {
		bool is_floating_or_child = container_is_floating_or_child(cont);
		bool is_fullscreen_or_child = container_is_fullscreen_or_child(cont);
		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
		bool mod_pressed = keyboard &&
			(wlr_keyboard_get_modifiers(keyboard) & config->floating_mod);

		// Handle beginning floating move
		if (is_floating_or_child && !is_fullscreen_or_child && mod_pressed) {
			seat_set_focus_container(seat,
				seat_get_focus_inactive_view(seat, &cont->node));
			seatop_begin_move_floating(seat, container_toplevel_ancestor(cont));
			return;
		}

		// Handle moving a tiling container
		if (config->tiling_drag && mod_pressed && !is_floating_or_child &&
				cont->pending.fullscreen_mode == FULLSCREEN_NONE) {
			seatop_begin_move_tiling(seat, cont);
			return;
		}

		// Handle tapping on a container surface
		seat_set_focus_container(seat, cont);
		seatop_begin_down(seat, node->sway_container, time_msec, sx, sy);
	}
#if HAVE_XWAYLAND
	// Handle tapping on an xwayland unmanaged view
	else if (wlr_surface_is_xwayland_surface(surface)) {
		struct wlr_xwayland_surface *xsurface =
				wlr_xwayland_surface_from_wlr_surface(surface);
		if (xsurface->override_redirect &&
				wlr_xwayland_or_surface_wants_focus(xsurface)) {
			struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
			wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
			seat_set_focus_surface(seat, xsurface->surface, false);
			transaction_commit_dirty();
		}
	}
#endif

	wlr_tablet_v2_tablet_tool_notify_down(tool->tablet_v2_tool);
	wlr_tablet_tool_v2_start_implicit_grab(tool->tablet_v2_tool);
}

/*----------------------------------\
 * Functions used by handle_button  /
 *--------------------------------*/

static bool trigger_pointer_button_binding(struct sway_seat *seat,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state, uint32_t modifiers,
		bool on_titlebar, bool on_border, bool on_contents, bool on_workspace) {
	// We can reach this for non-pointer devices if we're currently emulating
	// pointer input for one. Emulated input should not trigger bindings. The
	// device can be NULL if this is synthetic (e.g. swaymsg-generated) input.
	if (device && device->type != WLR_INPUT_DEVICE_POINTER) {
		return false;
	}

	struct seatop_default_event *e = seat->seatop_data;

	char *device_identifier = device ? input_device_get_identifier(device)
		: strdup("*");
	struct sway_binding *binding = NULL;
	if (state == WLR_BUTTON_PRESSED) {
		state_add_button(e, button);
		binding = get_active_mouse_binding(e,
			config->current_mode->mouse_bindings, modifiers, false,
			on_titlebar, on_border, on_contents, on_workspace,
			device_identifier);
	} else {
		binding = get_active_mouse_binding(e,
			config->current_mode->mouse_bindings, modifiers, true,
			on_titlebar, on_border, on_contents, on_workspace,
			device_identifier);
		state_erase_button(e, button);
	}

	free(device_identifier);
	if (binding) {
		seat_execute_command(seat, binding);
		return true;
	}

	return false;
}

static void handle_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	struct sway_cursor *cursor = seat->cursor;

	// Determine what's under the cursor
	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct sway_node *node = node_at_coords(seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

	struct sway_container *cont = node && node->type == N_CONTAINER ?
		node->sway_container : NULL;
	bool is_floating = cont && container_is_floating(cont);
	bool is_floating_or_child = cont && container_is_floating_or_child(cont);
	bool is_fullscreen_or_child = cont && container_is_fullscreen_or_child(cont);
	enum wlr_edges edge = cont ? find_edge(cont, surface, cursor) : WLR_EDGE_NONE;
	enum wlr_edges resize_edge = cont && edge ?
		find_resize_edge(cont, surface, cursor) : WLR_EDGE_NONE;
	bool on_border = edge != WLR_EDGE_NONE;
	bool on_contents = cont && !on_border && surface;
	bool on_workspace = node && node->type == N_WORKSPACE;
	bool on_titlebar = cont && !on_border && !surface;

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
	uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

	// Handle mouse bindings
	if (trigger_pointer_button_binding(seat, device, button, state, modifiers,
			on_titlebar, on_border, on_contents, on_workspace)) {
		return;
	}

	// Handle clicking an empty workspace
	if (node && node->type == N_WORKSPACE) {
		if (state == WLR_BUTTON_PRESSED) {
			seat_set_focus(seat, node);
			transaction_commit_dirty();
		}
		seat_pointer_notify_button(seat, time_msec, button, state);
		return;
	}

	// Handle clicking a layer surface
	if (surface && wlr_surface_is_layer_surface(surface)) {
		struct wlr_layer_surface_v1 *layer =
			wlr_layer_surface_v1_from_wlr_surface(surface);
		if (layer->current.keyboard_interactive) {
			seat_set_focus_layer(seat, layer);
			transaction_commit_dirty();
		}
		seat_pointer_notify_button(seat, time_msec, button, state);
		return;
	}

	// Handle tiling resize via border
	if (cont && resize_edge && button == BTN_LEFT &&
			state == WLR_BUTTON_PRESSED && !is_floating) {
		// If a resize is triggered on a tabbed or stacked container, change
		// focus to the tab which already had inactive focus -- otherwise, we'd
		// change the active tab when the user probably just wanted to resize.
		struct sway_container *cont_to_focus = cont;
		enum sway_container_layout layout = container_parent_layout(cont);
		if (layout == L_TABBED || layout == L_STACKED) {
			cont_to_focus = seat_get_focus_inactive_view(seat, &cont->pending.parent->node);
		}

		seat_set_focus_container(seat, cont_to_focus);
		seatop_begin_resize_tiling(seat, cont, edge);
		return;
	}

	// Handle tiling resize via mod
	bool mod_pressed = modifiers & config->floating_mod;
	if (cont && !is_floating_or_child && mod_pressed &&
			state == WLR_BUTTON_PRESSED) {
		uint32_t btn_resize = config->floating_mod_inverse ?
			BTN_LEFT : BTN_RIGHT;
		if (button == btn_resize) {
			edge = 0;
			edge |= cursor->cursor->x > cont->pending.x + cont->pending.width / 2 ?
				WLR_EDGE_RIGHT : WLR_EDGE_LEFT;
			edge |= cursor->cursor->y > cont->pending.y + cont->pending.height / 2 ?
				WLR_EDGE_BOTTOM : WLR_EDGE_TOP;

			const char *image = NULL;
			if (edge == (WLR_EDGE_LEFT | WLR_EDGE_TOP)) {
				image = "nw-resize";
			} else if (edge == (WLR_EDGE_TOP | WLR_EDGE_RIGHT)) {
				image = "ne-resize";
			} else if (edge == (WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM)) {
				image = "se-resize";
			} else if (edge == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) {
				image = "sw-resize";
			}
			cursor_set_image(seat->cursor, image, NULL);
			seat_set_focus_container(seat, cont);
			seatop_begin_resize_tiling(seat, cont, edge);
			return;
		}
	}

	// Handle beginning floating move
	if (cont && is_floating_or_child && !is_fullscreen_or_child &&
			state == WLR_BUTTON_PRESSED) {
		uint32_t btn_move = config->floating_mod_inverse ? BTN_RIGHT : BTN_LEFT;
		if (button == btn_move && (mod_pressed || on_titlebar)) {
			seat_set_focus_container(seat,
					seat_get_focus_inactive_view(seat, &cont->node));
			seatop_begin_move_floating(seat, container_toplevel_ancestor(cont));
			return;
		}
	}

	// Handle beginning floating resize
	if (cont && is_floating_or_child && !is_fullscreen_or_child &&
			state == WLR_BUTTON_PRESSED) {
		// Via border
		if (button == BTN_LEFT && resize_edge != WLR_EDGE_NONE) {
			seatop_begin_resize_floating(seat, cont, resize_edge);
			return;
		}

		// Via mod+click
		uint32_t btn_resize = config->floating_mod_inverse ?
			BTN_LEFT : BTN_RIGHT;
		if (mod_pressed && button == btn_resize) {
			struct sway_container *floater = container_toplevel_ancestor(cont);
			edge = 0;
			edge |= cursor->cursor->x > floater->pending.x + floater->pending.width / 2 ?
				WLR_EDGE_RIGHT : WLR_EDGE_LEFT;
			edge |= cursor->cursor->y > floater->pending.y + floater->pending.height / 2 ?
				WLR_EDGE_BOTTOM : WLR_EDGE_TOP;
			seatop_begin_resize_floating(seat, floater, edge);
			return;
		}
	}

	// Handle moving a tiling container
	if (config->tiling_drag && (mod_pressed || on_titlebar) &&
			state == WLR_BUTTON_PRESSED && !is_floating_or_child &&
			cont && cont->pending.fullscreen_mode == FULLSCREEN_NONE) {
		struct sway_container *focus = seat_get_focused_container(seat);
		bool focused = focus == cont || container_has_ancestor(focus, cont);
		if (on_titlebar && !focused) {
			node = seat_get_focus_inactive(seat, &cont->node);
			seat_set_focus(seat, node);
		}

		// If moving a container by it's title bar, use a threshold for the drag
		if (!mod_pressed && config->tiling_drag_threshold > 0) {
			seatop_begin_move_tiling_threshold(seat, cont);
		} else {
			seatop_begin_move_tiling(seat, cont);
		}
		return;
	}

	// Handle mousedown on a container surface
	if (surface && cont && state == WLR_BUTTON_PRESSED) {
		seat_set_focus_container(seat, cont);
		seatop_begin_down(seat, cont, time_msec, sx, sy);
		seat_pointer_notify_button(seat, time_msec, button, WLR_BUTTON_PRESSED);
		return;
	}

	// Handle clicking a container surface or decorations
	if (cont && state == WLR_BUTTON_PRESSED) {
		node = seat_get_focus_inactive(seat, &cont->node);
		seat_set_focus(seat, node);
		transaction_commit_dirty();
		seat_pointer_notify_button(seat, time_msec, button, state);
		return;
	}

#if HAVE_XWAYLAND
	// Handle clicking on xwayland unmanaged view
	if (surface && wlr_surface_is_xwayland_surface(surface)) {
		struct wlr_xwayland_surface *xsurface =
			wlr_xwayland_surface_from_wlr_surface(surface);
		if (xsurface->override_redirect &&
				wlr_xwayland_or_surface_wants_focus(xsurface)) {
			struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
			wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
			seat_set_focus_surface(seat, xsurface->surface, false);
			transaction_commit_dirty();
			seat_pointer_notify_button(seat, time_msec, button, state);
			return;
		}
	}
#endif

	seat_pointer_notify_button(seat, time_msec, button, state);
}

/*------------------------------------------\
 * Functions used by handle_pointer_motion  /
 *----------------------------------------*/

static void check_focus_follows_mouse(struct sway_seat *seat,
		struct seatop_default_event *e, struct sway_node *hovered_node) {
	struct sway_node *focus = seat_get_focus(seat);

	// This is the case if a layer-shell surface is hovered.
	// If it's on another output, focus the active workspace there.
	if (!hovered_node) {
		struct wlr_output *wlr_output = wlr_output_layout_output_at(
				root->output_layout, seat->cursor->cursor->x, seat->cursor->cursor->y);
		if (wlr_output == NULL) {
			return;
		}
		struct sway_output *hovered_output = wlr_output->data;
		if (focus && hovered_output != node_get_output(focus)) {
			struct sway_workspace *ws = output_get_active_workspace(hovered_output);
			seat_set_focus(seat, &ws->node);
			transaction_commit_dirty();
		}
		return;
	}

	// If a workspace node is hovered (eg. in the gap area), only set focus if
	// the workspace is on a different output to the previous focus.
	if (focus && hovered_node->type == N_WORKSPACE) {
		struct sway_output *focused_output = node_get_output(focus);
		struct sway_output *hovered_output = node_get_output(hovered_node);
		if (hovered_output != focused_output) {
			seat_set_focus(seat, seat_get_focus_inactive(seat, hovered_node));
			transaction_commit_dirty();
		}
		return;
	}

	// This is where we handle the common case. We don't want to focus inactive
	// tabs, hence the view_is_visible check.
	if (node_is_view(hovered_node) &&
			view_is_visible(hovered_node->sway_container->view)) {
		// e->previous_node is the node which the cursor was over previously.
		// If focus_follows_mouse is yes and the cursor got over the view due
		// to, say, a workspace switch, we don't want to set the focus.
		// But if focus_follows_mouse is "always", we do.
		if (hovered_node != e->previous_node ||
				config->focus_follows_mouse == FOLLOWS_ALWAYS) {
			seat_set_focus(seat, hovered_node);
			transaction_commit_dirty();
		}
	}
}

static void handle_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_default_event *e = seat->seatop_data;
	struct sway_cursor *cursor = seat->cursor;

	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct sway_node *node = node_at_coords(seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

	if (config->focus_follows_mouse != FOLLOWS_NO) {
		check_focus_follows_mouse(seat, e, node);
	}

	if (surface) {
		if (seat_is_input_allowed(seat, surface)) {
			wlr_seat_pointer_notify_enter(seat->wlr_seat, surface, sx, sy);
			wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
		}
	} else {
		cursor_update_image(cursor, node);
		wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
	}

	struct sway_drag_icon *drag_icon;
	wl_list_for_each(drag_icon, &root->drag_icons, link) {
		if (drag_icon->seat == seat) {
			drag_icon_update_position(drag_icon);
		}
	}

	e->previous_node = node;
}

static void handle_tablet_tool_motion(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec) {
	struct seatop_default_event *e = seat->seatop_data;
	struct sway_cursor *cursor = seat->cursor;

	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct sway_node *node = node_at_coords(seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

	if (config->focus_follows_mouse != FOLLOWS_NO) {
		check_focus_follows_mouse(seat, e, node);
	}

	if (surface) {
		if (seat_is_input_allowed(seat, surface)) {
			wlr_tablet_v2_tablet_tool_notify_proximity_in(tool->tablet_v2_tool,
				tool->tablet->tablet_v2, surface);
			wlr_tablet_v2_tablet_tool_notify_motion(tool->tablet_v2_tool, sx, sy);
		}
	} else {
		cursor_update_image(cursor, node);
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->tablet_v2_tool);
	}

	struct sway_drag_icon *drag_icon;
	wl_list_for_each(drag_icon, &root->drag_icons, link) {
		if (drag_icon->seat == seat) {
			drag_icon_update_position(drag_icon);
		}
	}

	e->previous_node = node;
}

/*----------------------------------------\
 * Functions used by handle_pointer_axis  /
 *--------------------------------------*/

static uint32_t wl_axis_to_button(struct wlr_event_pointer_axis *event) {
	switch (event->orientation) {
	case WLR_AXIS_ORIENTATION_VERTICAL:
		return event->delta < 0 ? SWAY_SCROLL_UP : SWAY_SCROLL_DOWN;
	case WLR_AXIS_ORIENTATION_HORIZONTAL:
		return event->delta < 0 ? SWAY_SCROLL_LEFT : SWAY_SCROLL_RIGHT;
	default:
		sway_log(SWAY_DEBUG, "Unknown axis orientation");
		return 0;
	}
}

static void handle_pointer_axis(struct sway_seat *seat,
		struct wlr_event_pointer_axis *event) {
	struct sway_input_device *input_device =
		event->device ? event->device->data : NULL;
	struct input_config *ic =
		input_device ? input_device_get_config(input_device) : NULL;
	struct sway_cursor *cursor = seat->cursor;
	struct seatop_default_event *e = seat->seatop_data;

	// Determine what's under the cursor
	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct sway_node *node = node_at_coords(seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);
	struct sway_container *cont = node && node->type == N_CONTAINER ?
		node->sway_container : NULL;
	enum wlr_edges edge = cont ? find_edge(cont, surface, cursor) : WLR_EDGE_NONE;
	bool on_border = edge != WLR_EDGE_NONE;
	bool on_titlebar = cont && !on_border && !surface;
	bool on_titlebar_border = cont && on_border &&
		cursor->cursor->y < cont->pending.content_y;
	bool on_contents = cont && !on_border && surface;
	bool on_workspace = node && node->type == N_WORKSPACE;
	float scroll_factor =
		(ic == NULL || ic->scroll_factor == FLT_MIN) ? 1.0f : ic->scroll_factor;

	bool handled = false;

	// Gather information needed for mouse bindings
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
	uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
	struct wlr_input_device *device =
		input_device ? input_device->wlr_device : NULL;
	char *dev_id = device ? input_device_get_identifier(device) : strdup("*");
	uint32_t button = wl_axis_to_button(event);

	// Handle mouse bindings - x11 mouse buttons 4-7 - press event
	struct sway_binding *binding = NULL;
	state_add_button(e, button);
	binding = get_active_mouse_binding(e, config->current_mode->mouse_bindings,
			modifiers, false, on_titlebar, on_border, on_contents, on_workspace,
			dev_id);
	if (binding) {
		seat_execute_command(seat, binding);
		handled = true;
	}

	// Scrolling on a tabbed or stacked title bar (handled as press event)
	if (!handled && (on_titlebar || on_titlebar_border)) {
		enum sway_container_layout layout = container_parent_layout(cont);
		if (layout == L_TABBED || layout == L_STACKED) {
			struct sway_node *tabcontainer = node_get_parent(node);
			struct sway_node *active =
				seat_get_active_tiling_child(seat, tabcontainer);
			list_t *siblings = container_get_siblings(cont);
			int desired = list_find(siblings, active->sway_container) +
				round(scroll_factor * event->delta_discrete);
			if (desired < 0) {
				desired = 0;
			} else if (desired >= siblings->length) {
				desired = siblings->length - 1;
			}

			struct sway_container *new_sibling_con = siblings->items[desired];
			struct sway_node *new_sibling = &new_sibling_con->node;
			struct sway_node *new_focus =
				seat_get_focus_inactive(seat, new_sibling);
			// Use the focused child of the tabbed/stacked container, not the
			// container the user scrolled on.
			seat_set_focus(seat, new_focus);
			transaction_commit_dirty();
			handled = true;
		}
	}

	// Handle mouse bindings - x11 mouse buttons 4-7 - release event
	binding = get_active_mouse_binding(e, config->current_mode->mouse_bindings,
			modifiers, true, on_titlebar, on_border, on_contents, on_workspace,
			dev_id);
	state_erase_button(e, button);
	if (binding) {
		seat_execute_command(seat, binding);
		handled = true;
	}
	free(dev_id);

	if (!handled) {
		wlr_seat_pointer_notify_axis(cursor->seat->wlr_seat, event->time_msec,
			event->orientation, scroll_factor * event->delta,
			round(scroll_factor * event->delta_discrete), event->source);
	}
}

/*----------------------------------\
 * Functions used by handle_rebase  /
 *--------------------------------*/

static void handle_rebase(struct sway_seat *seat, uint32_t time_msec) {
	struct seatop_default_event *e = seat->seatop_data;
	struct sway_cursor *cursor = seat->cursor;
	struct wlr_surface *surface = NULL;
	double sx = 0.0, sy = 0.0;
	e->previous_node = node_at_coords(seat,
			cursor->cursor->x, cursor->cursor->y, &surface, &sx, &sy);

	if (surface) {
		if (seat_is_input_allowed(seat, surface)) {
			wlr_seat_pointer_notify_enter(seat->wlr_seat, surface, sx, sy);
			wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
		}
	} else {
		cursor_update_image(cursor, e->previous_node);
		wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
	}
}

static const struct sway_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.pointer_axis = handle_pointer_axis,
	.tablet_tool_tip = handle_tablet_tool_tip,
	.tablet_tool_motion = handle_tablet_tool_motion,
	.rebase = handle_rebase,
	.allow_set_cursor = true,
};

void seatop_begin_default(struct sway_seat *seat) {
	seatop_end(seat);

	struct seatop_default_event *e =
		calloc(1, sizeof(struct seatop_default_event));
	sway_assert(e, "Unable to allocate seatop_default_event");
	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	seatop_rebase(seat, 0);
}
