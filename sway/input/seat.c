#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <linux/input-event-codes.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "config.h"
#include "list.h"
#include "log.h"
#include "sway/config.h"
#include "sway/desktop.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/input/keyboard.h"
#include "sway/input/libinput.h"
#include "sway/input/seat.h"
#include "sway/input/switch.h"
#include "sway/input/tablet.h"
#include "sway/ipc-server.h"
#include "sway/layers.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

static void seat_device_destroy(struct sway_seat_device *seat_device) {
	if (!seat_device) {
		return;
	}

	sway_keyboard_destroy(seat_device->keyboard);
	sway_tablet_destroy(seat_device->tablet);
	sway_tablet_pad_destroy(seat_device->tablet_pad);
	wlr_cursor_detach_input_device(seat_device->sway_seat->cursor->cursor,
		seat_device->input_device->wlr_device);
	wl_list_remove(&seat_device->link);
	free(seat_device);
}

static void seat_node_destroy(struct sway_seat_node *seat_node) {
	wl_list_remove(&seat_node->destroy.link);
	wl_list_remove(&seat_node->link);
	free(seat_node);
}

void seat_destroy(struct sway_seat *seat) {
	if (seat == config->handler_context.seat) {
		config->handler_context.seat = input_manager_get_default_seat();
	}
	struct sway_seat_device *seat_device, *next;
	wl_list_for_each_safe(seat_device, next, &seat->devices, link) {
		seat_device_destroy(seat_device);
	}
	struct sway_seat_node *seat_node, *next_seat_node;
	wl_list_for_each_safe(seat_node, next_seat_node, &seat->focus_stack,
			link) {
		seat_node_destroy(seat_node);
	}
	sway_input_method_relay_finish(&seat->im_relay);
	sway_cursor_destroy(seat->cursor);
	wl_list_remove(&seat->new_node.link);
	wl_list_remove(&seat->request_start_drag.link);
	wl_list_remove(&seat->start_drag.link);
	wl_list_remove(&seat->request_set_selection.link);
	wl_list_remove(&seat->request_set_primary_selection.link);
	wl_list_remove(&seat->link);
	wlr_seat_destroy(seat->wlr_seat);
	for (int i = 0; i < seat->deferred_bindings->length; i++) {
		free_sway_binding(seat->deferred_bindings->items[i]);
	}
	list_free(seat->deferred_bindings);
	free(seat->prev_workspace_name);
	free(seat);
}

void seat_idle_notify_activity(struct sway_seat *seat,
		enum sway_input_idle_source source) {
	uint32_t mask = seat->idle_inhibit_sources;
	struct wlr_idle_timeout *timeout;
	int ntimers = 0, nidle = 0;
	wl_list_for_each(timeout, &server.idle->idle_timers, link) {
		++ntimers;
		if (timeout->idle_state) {
			++nidle;
		}
	}
	if (nidle == ntimers) {
		mask = seat->idle_wake_sources;
	}
	if ((source & mask) > 0) {
		wlr_idle_notify_activity(server.idle, seat->wlr_seat);
	}
}

/**
 * Activate all views within this container recursively.
 */
static void seat_send_activate(struct sway_node *node, struct sway_seat *seat) {
	if (node_is_view(node)) {
		if (!seat_is_input_allowed(seat, node->sway_container->view->surface)) {
			sway_log(SWAY_DEBUG, "Refusing to set focus, input is inhibited");
			return;
		}
		view_set_activated(node->sway_container->view, true);
	} else {
		list_t *children = node_get_children(node);
		for (int i = 0; i < children->length; ++i) {
			struct sway_container *child = children->items[i];
			seat_send_activate(&child->node, seat);
		}
	}
}

static struct sway_keyboard *sway_keyboard_for_wlr_keyboard(
		struct sway_seat *seat, struct wlr_keyboard *wlr_keyboard) {
	struct sway_seat_device *seat_device;
	wl_list_for_each(seat_device, &seat->devices, link) {
		struct sway_input_device *input_device = seat_device->input_device;
		if (input_device->wlr_device->type != WLR_INPUT_DEVICE_KEYBOARD) {
			continue;
		}
		if (input_device->wlr_device->keyboard == wlr_keyboard) {
			return seat_device->keyboard;
		}
	}
	struct sway_keyboard_group *group;
	wl_list_for_each(group, &seat->keyboard_groups, link) {
		struct sway_input_device *input_device =
			group->seat_device->input_device;
		if (input_device->wlr_device->keyboard == wlr_keyboard) {
			return group->seat_device->keyboard;
		}
	}
	return NULL;
}

static void seat_keyboard_notify_enter(struct sway_seat *seat,
		struct wlr_surface *surface) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
	if (!keyboard) {
		wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface, NULL, 0, NULL);
		return;
	}

	struct sway_keyboard *sway_keyboard =
		sway_keyboard_for_wlr_keyboard(seat, keyboard);
	assert(sway_keyboard && "Cannot find sway_keyboard for seat keyboard");

	struct sway_shortcut_state *state = &sway_keyboard->state_pressed_sent;
	wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface,
			state->pressed_keycodes, state->npressed, &keyboard->modifiers);
}

static void seat_tablet_pads_notify_enter(struct sway_seat *seat,
		struct wlr_surface *surface) {
	struct sway_seat_device *seat_device;
	wl_list_for_each(seat_device, &seat->devices, link) {
		sway_tablet_pad_notify_enter(seat_device->tablet_pad, surface);
	}
}

/**
 * If con is a view, set it as active and enable keyboard input.
 * If con is a container, set all child views as active and don't enable
 * keyboard input on any.
 */
static void seat_send_focus(struct sway_node *node, struct sway_seat *seat) {
	seat_send_activate(node, seat);

	struct sway_view *view = node->type == N_CONTAINER ?
		node->sway_container->view : NULL;

	if (view && seat_is_input_allowed(seat, view->surface)) {
#if HAVE_XWAYLAND
		if (view->type == SWAY_VIEW_XWAYLAND) {
			struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
			wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
		}
#endif

		seat_keyboard_notify_enter(seat, view->surface);
		seat_tablet_pads_notify_enter(seat, view->surface);
		sway_input_method_relay_set_focus(&seat->im_relay, view->surface);

		struct wlr_pointer_constraint_v1 *constraint =
			wlr_pointer_constraints_v1_constraint_for_surface(
				server.pointer_constraints, view->surface, seat->wlr_seat);
		sway_cursor_constrain(seat->cursor, constraint);
	}
}

void seat_for_each_node(struct sway_seat *seat,
		void (*f)(struct sway_node *node, void *data), void *data) {
	struct sway_seat_node *current = NULL;
	wl_list_for_each(current, &seat->focus_stack, link) {
		f(current->node, data);
	}
}

struct sway_container *seat_get_focus_inactive_view(struct sway_seat *seat,
		struct sway_node *ancestor) {
	if (ancestor->type == N_CONTAINER && ancestor->sway_container->view) {
		return ancestor->sway_container;
	}
	struct sway_seat_node *current;
	wl_list_for_each(current, &seat->focus_stack, link) {
		struct sway_node *node = current->node;
		if (node->type == N_CONTAINER && node->sway_container->view &&
				node_has_ancestor(node, ancestor)) {
			return node->sway_container;
		}
	}
	return NULL;
}

static void handle_seat_node_destroy(struct wl_listener *listener, void *data) {
	struct sway_seat_node *seat_node =
		wl_container_of(listener, seat_node, destroy);
	struct sway_seat *seat = seat_node->seat;
	struct sway_node *node = seat_node->node;
	struct sway_node *parent = node_get_parent(node);
	struct sway_node *focus = seat_get_focus(seat);

	if (node->type == N_WORKSPACE) {
		seat_node_destroy(seat_node);
		// If an unmanaged or layer surface is focused when an output gets
		// disabled and an empty workspace on the output was focused by the
		// seat, the seat needs to refocus it's focus inactive to update the
		// value of seat->workspace.
		if (seat->workspace == node->sway_workspace) {
			struct sway_node *node = seat_get_focus_inactive(seat, &root->node);
			seat_set_focus(seat, NULL);
			if (node) {
				seat_set_focus(seat, node);
			} else {
				seat->workspace = NULL;
			}
		}
		return;
	}

	// Even though the container being destroyed might be nowhere near the
	// focused container, we still need to set focus_inactive on a sibling of
	// the container being destroyed.
	bool needs_new_focus = focus &&
		(focus == node || node_has_ancestor(focus, node));

	seat_node_destroy(seat_node);

	if (!parent && !needs_new_focus) {
		// Destroying a container that is no longer in the tree
		return;
	}

	// Find new focus_inactive (ie. sibling, or workspace if no siblings left)
	struct sway_node *next_focus = NULL;
	while (next_focus == NULL && parent != NULL) {
		struct sway_container *con =
			seat_get_focus_inactive_view(seat, parent);
		next_focus = con ? &con->node : NULL;

		if (next_focus == NULL && parent->type == N_WORKSPACE) {
			next_focus = parent;
			break;
		}

		parent = node_get_parent(parent);
	}

	if (!next_focus) {
		struct sway_workspace *ws = seat_get_last_known_workspace(seat);
		if (!ws) {
			return;
		}
		struct sway_container *con =
			seat_get_focus_inactive_view(seat, &ws->node);
		next_focus = con ? &(con->node) : &(ws->node);
	}

	if (next_focus->type == N_WORKSPACE &&
			!workspace_is_visible(next_focus->sway_workspace)) {
		// Do not change focus to a non-visible workspace
		return;
	}

	if (needs_new_focus) {
		// Make sure the workspace IPC event gets sent
		if (node->type == N_CONTAINER && node->sway_container->scratchpad) {
			seat_set_focus(seat, NULL);
		}
		// The structure change might have caused it to move up to the top of
		// the focus stack without sending focus notifications to the view
		if (seat_get_focus(seat) == next_focus) {
			seat_send_focus(next_focus, seat);
		} else {
			seat_set_focus(seat, next_focus);
		}
	} else {
		// Setting focus_inactive
		focus = seat_get_focus_inactive(seat, &root->node);
		seat_set_raw_focus(seat, next_focus);
		if (focus->type == N_CONTAINER && focus->sway_container->pending.workspace) {
			seat_set_raw_focus(seat, &focus->sway_container->pending.workspace->node);
		}
		seat_set_raw_focus(seat, focus);
	}
}

static struct sway_seat_node *seat_node_from_node(
		struct sway_seat *seat, struct sway_node *node) {
	if (node->type == N_ROOT || node->type == N_OUTPUT) {
		// these don't get seat nodes ever
		return NULL;
	}

	struct sway_seat_node *seat_node = NULL;
	wl_list_for_each(seat_node, &seat->focus_stack, link) {
		if (seat_node->node == node) {
			return seat_node;
		}
	}

	seat_node = calloc(1, sizeof(struct sway_seat_node));
	if (seat_node == NULL) {
		sway_log(SWAY_ERROR, "could not allocate seat node");
		return NULL;
	}

	seat_node->node = node;
	seat_node->seat = seat;
	wl_list_insert(seat->focus_stack.prev, &seat_node->link);
	wl_signal_add(&node->events.destroy, &seat_node->destroy);
	seat_node->destroy.notify = handle_seat_node_destroy;

	return seat_node;
}

static void handle_new_node(struct wl_listener *listener, void *data) {
	struct sway_seat *seat = wl_container_of(listener, seat, new_node);
	struct sway_node *node = data;
	seat_node_from_node(seat, node);
}

static void drag_icon_damage_whole(struct sway_drag_icon *icon) {
	if (!icon->wlr_drag_icon->mapped) {
		return;
	}
	desktop_damage_surface(icon->wlr_drag_icon->surface, icon->x, icon->y, true);
}

void drag_icon_update_position(struct sway_drag_icon *icon) {
	drag_icon_damage_whole(icon);

	struct wlr_drag_icon *wlr_icon = icon->wlr_drag_icon;
	struct sway_seat *seat = icon->seat;
	struct wlr_cursor *cursor = seat->cursor->cursor;
	switch (wlr_icon->drag->grab_type) {
	case WLR_DRAG_GRAB_KEYBOARD:
		return;
	case WLR_DRAG_GRAB_KEYBOARD_POINTER:
		icon->x = cursor->x;
		icon->y = cursor->y;
		break;
	case WLR_DRAG_GRAB_KEYBOARD_TOUCH:;
		struct wlr_touch_point *point =
			wlr_seat_touch_get_point(seat->wlr_seat, wlr_icon->drag->touch_id);
		if (point == NULL) {
			return;
		}
		icon->x = seat->touch_x;
		icon->y = seat->touch_y;
	}

	drag_icon_damage_whole(icon);
}

static void drag_icon_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct sway_drag_icon *icon =
		wl_container_of(listener, icon, surface_commit);
	drag_icon_update_position(icon);
}

static void drag_icon_handle_map(struct wl_listener *listener, void *data) {
	struct sway_drag_icon *icon = wl_container_of(listener, icon, map);
	drag_icon_damage_whole(icon);
}

static void drag_icon_handle_unmap(struct wl_listener *listener, void *data) {
	struct sway_drag_icon *icon = wl_container_of(listener, icon, unmap);
	drag_icon_damage_whole(icon);
}

static void drag_icon_handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_drag_icon *icon = wl_container_of(listener, icon, destroy);
	icon->wlr_drag_icon->data = NULL;
	wl_list_remove(&icon->link);
	wl_list_remove(&icon->surface_commit.link);
	wl_list_remove(&icon->unmap.link);
	wl_list_remove(&icon->map.link);
	wl_list_remove(&icon->destroy.link);
	free(icon);
}

static void drag_handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_drag *drag = wl_container_of(listener, drag, destroy);

	// Focus enter isn't sent during drag, so refocus the focused node, layer
	// surface or unmanaged surface.
	struct sway_seat *seat = drag->seat;
	struct sway_node *focus = seat_get_focus(seat);
	if (focus) {
		seat_set_focus(seat, NULL);
		seat_set_focus(seat, focus);
	} else if (seat->focused_layer) {
		struct wlr_layer_surface_v1 *layer = seat->focused_layer;
		seat_set_focus_layer(seat, NULL);
		seat_set_focus_layer(seat, layer);
	} else {
		struct wlr_surface *unmanaged = seat->wlr_seat->keyboard_state.focused_surface;
		seat_set_focus_surface(seat, NULL, false);
		seat_set_focus_surface(seat, unmanaged, false);
	}

	drag->wlr_drag->data = NULL;
	wl_list_remove(&drag->destroy.link);
	free(drag);
}

static void handle_request_start_drag(struct wl_listener *listener,
		void *data) {
	struct sway_seat *seat = wl_container_of(listener, seat, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat->wlr_seat,
			event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(seat->wlr_seat, event->drag, event->serial);
		return;
	}

	struct wlr_touch_point *point;
	if (wlr_seat_validate_touch_grab_serial(seat->wlr_seat,
			event->origin, event->serial, &point)) {
		wlr_seat_start_touch_drag(seat->wlr_seat,
			event->drag, event->serial, point);
		return;
	}

	// TODO: tablet grabs

	sway_log(SWAY_DEBUG, "Ignoring start_drag request: "
		"could not validate pointer or touch serial %" PRIu32, event->serial);
	wlr_data_source_destroy(event->drag->source);
}

static void handle_start_drag(struct wl_listener *listener, void *data) {
	struct sway_seat *seat = wl_container_of(listener, seat, start_drag);
	struct wlr_drag *wlr_drag = data;

	struct sway_drag *drag = calloc(1, sizeof(struct sway_drag));
	if (drag == NULL) {
		sway_log(SWAY_ERROR, "Allocation failed");
		return;
	}
	drag->seat = seat;
	drag->wlr_drag = wlr_drag;
	wlr_drag->data = drag;

	drag->destroy.notify = drag_handle_destroy;
	wl_signal_add(&wlr_drag->events.destroy, &drag->destroy);

	struct wlr_drag_icon *wlr_drag_icon = wlr_drag->icon;
	if (wlr_drag_icon != NULL) {
		struct sway_drag_icon *icon = calloc(1, sizeof(struct sway_drag_icon));
		if (icon == NULL) {
			sway_log(SWAY_ERROR, "Allocation failed");
			return;
		}
		icon->seat = seat;
		icon->wlr_drag_icon = wlr_drag_icon;
		wlr_drag_icon->data = icon;

		icon->surface_commit.notify = drag_icon_handle_surface_commit;
		wl_signal_add(&wlr_drag_icon->surface->events.commit, &icon->surface_commit);
		icon->unmap.notify = drag_icon_handle_unmap;
		wl_signal_add(&wlr_drag_icon->events.unmap, &icon->unmap);
		icon->map.notify = drag_icon_handle_map;
		wl_signal_add(&wlr_drag_icon->events.map, &icon->map);
		icon->destroy.notify = drag_icon_handle_destroy;
		wl_signal_add(&wlr_drag_icon->events.destroy, &icon->destroy);

		wl_list_insert(&root->drag_icons, &icon->link);

		drag_icon_update_position(icon);
	}
	seatop_begin_default(seat);
}

static void handle_request_set_selection(struct wl_listener *listener,
		void *data) {
	struct sway_seat *seat =
		wl_container_of(listener, seat, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat->wlr_seat, event->source, event->serial);
}

static void handle_request_set_primary_selection(struct wl_listener *listener,
		void *data) {
	struct sway_seat *seat =
		wl_container_of(listener, seat, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat->wlr_seat, event->source, event->serial);
}

static void collect_focus_iter(struct sway_node *node, void *data) {
	struct sway_seat *seat = data;
	struct sway_seat_node *seat_node = seat_node_from_node(seat, node);
	if (!seat_node) {
		return;
	}
	wl_list_remove(&seat_node->link);
	wl_list_insert(&seat->focus_stack, &seat_node->link);
}

static void collect_focus_workspace_iter(struct sway_workspace *workspace,
		void *data) {
	collect_focus_iter(&workspace->node, data);
}

static void collect_focus_container_iter(struct sway_container *container,
		void *data) {
	collect_focus_iter(&container->node, data);
}

struct sway_seat *seat_create(const char *seat_name) {
	struct sway_seat *seat = calloc(1, sizeof(struct sway_seat));
	if (!seat) {
		return NULL;
	}

	seat->wlr_seat = wlr_seat_create(server.wl_display, seat_name);
	if (!sway_assert(seat->wlr_seat, "could not allocate seat")) {
		free(seat);
		return NULL;
	}
	seat->wlr_seat->data = seat;

	seat->cursor = sway_cursor_create(seat);
	if (!seat->cursor) {
		wlr_seat_destroy(seat->wlr_seat);
		free(seat);
		return NULL;
	}

	seat->idle_inhibit_sources = seat->idle_wake_sources =
		IDLE_SOURCE_KEYBOARD |
		IDLE_SOURCE_POINTER |
		IDLE_SOURCE_TOUCH |
		IDLE_SOURCE_TABLET_PAD |
		IDLE_SOURCE_TABLET_TOOL |
		IDLE_SOURCE_SWITCH;

	// init the focus stack
	wl_list_init(&seat->focus_stack);

	wl_list_init(&seat->devices);

	root_for_each_workspace(collect_focus_workspace_iter, seat);
	root_for_each_container(collect_focus_container_iter, seat);

	seat->deferred_bindings = create_list();

	wl_signal_add(&root->events.new_node, &seat->new_node);
	seat->new_node.notify = handle_new_node;

	wl_signal_add(&seat->wlr_seat->events.request_start_drag,
		&seat->request_start_drag);
	seat->request_start_drag.notify = handle_request_start_drag;

	wl_signal_add(&seat->wlr_seat->events.start_drag, &seat->start_drag);
	seat->start_drag.notify = handle_start_drag;

	wl_signal_add(&seat->wlr_seat->events.request_set_selection,
		&seat->request_set_selection);
	seat->request_set_selection.notify = handle_request_set_selection;

	wl_signal_add(&seat->wlr_seat->events.request_set_primary_selection,
		&seat->request_set_primary_selection);
	seat->request_set_primary_selection.notify =
		handle_request_set_primary_selection;

	wl_list_init(&seat->keyboard_groups);
	wl_list_init(&seat->keyboard_shortcuts_inhibitors);

	sway_input_method_relay_init(seat, &seat->im_relay);

	bool first = wl_list_empty(&server.input->seats);
	wl_list_insert(&server.input->seats, &seat->link);

	if (!first) {
		// Since this is not the first seat, attempt to set initial focus
		struct sway_seat *current_seat = input_manager_current_seat();
		struct sway_node *current_focus =
			seat_get_focus_inactive(current_seat, &root->node);
		seat_set_focus(seat, current_focus);
	}

	seatop_begin_default(seat);

	return seat;
}

static void seat_update_capabilities(struct sway_seat *seat) {
	uint32_t caps = 0;
	uint32_t previous_caps = seat->wlr_seat->capabilities;
	struct sway_seat_device *seat_device;
	wl_list_for_each(seat_device, &seat->devices, link) {
		switch (seat_device->input_device->wlr_device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			caps |= WL_SEAT_CAPABILITY_KEYBOARD;
			break;
		case WLR_INPUT_DEVICE_POINTER:
			caps |= WL_SEAT_CAPABILITY_POINTER;
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			caps |= WL_SEAT_CAPABILITY_TOUCH;
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			caps |= WL_SEAT_CAPABILITY_POINTER;
			break;
		case WLR_INPUT_DEVICE_SWITCH:
		case WLR_INPUT_DEVICE_TABLET_PAD:
			break;
		}
	}

	// Hide cursor if seat doesn't have pointer capability.
	// We must call cursor_set_image while the wlr_seat has the capabilities
	// otherwise it's a no op.
	if ((caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
		cursor_set_image(seat->cursor, NULL, NULL);
		wlr_seat_set_capabilities(seat->wlr_seat, caps);
	} else {
		wlr_seat_set_capabilities(seat->wlr_seat, caps);
		if ((previous_caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
			cursor_set_image(seat->cursor, "left_ptr", NULL);
		}
	}
}

static void seat_reset_input_config(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	sway_log(SWAY_DEBUG, "Resetting output mapping for input device %s",
		sway_device->input_device->identifier);
	wlr_cursor_map_input_to_output(seat->cursor->cursor,
		sway_device->input_device->wlr_device, NULL);
}

static bool has_prefix(const char *str, const char *prefix) {
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

/**
 * Get the name of the built-in output, if any. Returns NULL if there isn't
 * exactly one built-in output.
 */
static const char *get_builtin_output_name(void) {
	const char *match = NULL;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		const char *name = output->wlr_output->name;
		if (has_prefix(name, "eDP-") || has_prefix(name, "LVDS-") ||
				has_prefix(name, "DSI-")) {
			if (match != NULL) {
				return NULL;
			}
			match = name;
		}
	}
	return match;
}

static bool is_touch_or_tablet_tool(struct sway_seat_device *seat_device) {
	switch (seat_device->input_device->wlr_device->type) {
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return true;
	default:
		return false;
	}
}

static void seat_apply_input_config(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	struct input_config *ic =
		input_device_get_config(sway_device->input_device);

	sway_log(SWAY_DEBUG, "Applying input config to %s",
		sway_device->input_device->identifier);

	const char *mapped_to_output = ic == NULL ? NULL : ic->mapped_to_output;
	struct wlr_box *mapped_to_region = ic == NULL ? NULL : ic->mapped_to_region;
	enum input_config_mapped_to mapped_to =
		ic == NULL ? MAPPED_TO_DEFAULT : ic->mapped_to;

	switch (mapped_to) {
	case MAPPED_TO_DEFAULT:
		/*
		 * If the wlroots backend provides an output name, use that.
		 *
		 * Otherwise, try to map built-in touch and tablet tool devices to the
		 * built-in output.
		 */
		mapped_to_output = sway_device->input_device->wlr_device->output_name;
		if (mapped_to_output == NULL && is_touch_or_tablet_tool(sway_device) &&
				sway_libinput_device_is_builtin(sway_device->input_device)) {
			mapped_to_output = get_builtin_output_name();
			if (mapped_to_output) {
				sway_log(SWAY_DEBUG, "Auto-detected output '%s' for device '%s'",
					mapped_to_output, sway_device->input_device->identifier);
			}
		}
		if (mapped_to_output == NULL) {
			return;
		}
		/* fallthrough */
	case MAPPED_TO_OUTPUT:
		sway_log(SWAY_DEBUG, "Mapping input device %s to output %s",
			sway_device->input_device->identifier, mapped_to_output);
		if (strcmp("*", mapped_to_output) == 0) {
			wlr_cursor_map_input_to_output(seat->cursor->cursor,
				sway_device->input_device->wlr_device, NULL);
			wlr_cursor_map_input_to_region(seat->cursor->cursor,
				sway_device->input_device->wlr_device, NULL);
			sway_log(SWAY_DEBUG, "Reset output mapping");
			return;
		}
		struct sway_output *output = output_by_name_or_id(mapped_to_output);
		if (!output) {
			sway_log(SWAY_DEBUG, "Requested output %s for device %s isn't present",
				mapped_to_output, sway_device->input_device->identifier);
			return;
		}
		wlr_cursor_map_input_to_output(seat->cursor->cursor,
			sway_device->input_device->wlr_device, output->wlr_output);
		wlr_cursor_map_input_to_region(seat->cursor->cursor,
			sway_device->input_device->wlr_device, NULL);
		sway_log(SWAY_DEBUG,
			"Mapped to output %s", output->wlr_output->name);
		return;
	case MAPPED_TO_REGION:
		sway_log(SWAY_DEBUG, "Mapping input device %s to %d,%d %dx%d",
			sway_device->input_device->identifier,
			mapped_to_region->x, mapped_to_region->y,
			mapped_to_region->width, mapped_to_region->height);
		wlr_cursor_map_input_to_output(seat->cursor->cursor,
			sway_device->input_device->wlr_device, NULL);
		wlr_cursor_map_input_to_region(seat->cursor->cursor,
			sway_device->input_device->wlr_device, mapped_to_region);
		return;
	}
}

static void seat_configure_pointer(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	if ((seat->wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER) == 0) {
		seat_configure_xcursor(seat);
	}
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		sway_device->input_device->wlr_device);
	seat_apply_input_config(seat, sway_device);
	wl_event_source_timer_update(
			seat->cursor->hide_source, cursor_get_timeout(seat->cursor));
}

static void seat_configure_keyboard(struct sway_seat *seat,
		struct sway_seat_device *seat_device) {
	if (!seat_device->keyboard) {
		sway_keyboard_create(seat, seat_device);
	}
	sway_keyboard_configure(seat_device->keyboard);
	wlr_seat_set_keyboard(seat->wlr_seat,
			seat_device->input_device->wlr_device);
	struct sway_node *focus = seat_get_focus(seat);
	if (focus && node_is_view(focus)) {
		// force notify reenter to pick up the new configuration
		wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
		seat_keyboard_notify_enter(seat, focus->sway_container->view->surface);
	}
}

static void seat_configure_switch(struct sway_seat *seat,
		struct sway_seat_device *seat_device) {
	if (!seat_device->switch_device) {
		sway_switch_create(seat, seat_device);
	}
	seat_apply_input_config(seat, seat_device);
	sway_switch_configure(seat_device->switch_device);
}

static void seat_configure_touch(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		sway_device->input_device->wlr_device);
	seat_apply_input_config(seat, sway_device);
}

static void seat_configure_tablet_tool(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	if (!sway_device->tablet) {
		sway_device->tablet = sway_tablet_create(seat, sway_device);
	}
	sway_configure_tablet(sway_device->tablet);
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		sway_device->input_device->wlr_device);
	seat_apply_input_config(seat, sway_device);
}

static void seat_configure_tablet_pad(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	if (!sway_device->tablet_pad) {
		sway_device->tablet_pad = sway_tablet_pad_create(seat, sway_device);
	}
	sway_configure_tablet_pad(sway_device->tablet_pad);
}

static struct sway_seat_device *seat_get_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	struct sway_seat_device *seat_device = NULL;
	wl_list_for_each(seat_device, &seat->devices, link) {
		if (seat_device->input_device == input_device) {
			return seat_device;
		}
	}

	struct sway_keyboard_group *group = NULL;
	wl_list_for_each(group, &seat->keyboard_groups, link) {
		if (group->seat_device->input_device == input_device) {
			return group->seat_device;
		}
	}

	return NULL;
}

void seat_configure_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	struct sway_seat_device *seat_device = seat_get_device(seat, input_device);
	if (!seat_device) {
		return;
	}

	switch (input_device->wlr_device->type) {
		case WLR_INPUT_DEVICE_POINTER:
			seat_configure_pointer(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_KEYBOARD:
			seat_configure_keyboard(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_SWITCH:
			seat_configure_switch(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			seat_configure_touch(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			seat_configure_tablet_tool(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			seat_configure_tablet_pad(seat, seat_device);
			break;
	}
}

void seat_reset_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	struct sway_seat_device *seat_device = seat_get_device(seat, input_device);
	if (!seat_device) {
		return;
	}

	switch (input_device->wlr_device->type) {
		case WLR_INPUT_DEVICE_POINTER:
			seat_reset_input_config(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_KEYBOARD:
			sway_keyboard_disarm_key_repeat(seat_device->keyboard);
			sway_keyboard_configure(seat_device->keyboard);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			seat_reset_input_config(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			seat_reset_input_config(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			sway_log(SWAY_DEBUG, "TODO: reset tablet pad");
			break;
		case WLR_INPUT_DEVICE_SWITCH:
			sway_log(SWAY_DEBUG, "TODO: reset switch device");
			break;
	}
}

void seat_add_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	if (seat_get_device(seat, input_device)) {
		seat_configure_device(seat, input_device);
		return;
	}

	struct sway_seat_device *seat_device =
		calloc(1, sizeof(struct sway_seat_device));
	if (!seat_device) {
		sway_log(SWAY_DEBUG, "could not allocate seat device");
		return;
	}

	sway_log(SWAY_DEBUG, "adding device %s to seat %s",
		input_device->identifier, seat->wlr_seat->name);

	seat_device->sway_seat = seat;
	seat_device->input_device = input_device;
	wl_list_insert(&seat->devices, &seat_device->link);

	seat_configure_device(seat, input_device);

	seat_update_capabilities(seat);
}

void seat_remove_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	struct sway_seat_device *seat_device = seat_get_device(seat, input_device);

	if (!seat_device) {
		return;
	}

	sway_log(SWAY_DEBUG, "removing device %s from seat %s",
		input_device->identifier, seat->wlr_seat->name);

	seat_device_destroy(seat_device);

	seat_update_capabilities(seat);
}

static bool xcursor_manager_is_named(const struct wlr_xcursor_manager *manager,
		const char *name) {
	return (!manager->name && !name) ||
		(name && manager->name && strcmp(name, manager->name) == 0);
}

void seat_configure_xcursor(struct sway_seat *seat) {
	unsigned cursor_size = 24;
	const char *cursor_theme = NULL;

	const struct seat_config *seat_config = seat_get_config(seat);
	if (!seat_config) {
		seat_config = seat_get_config_by_name("*");
	}
	if (seat_config) {
		cursor_size = seat_config->xcursor_theme.size;
		cursor_theme = seat_config->xcursor_theme.name;
	}

	if (seat == input_manager_get_default_seat()) {
		char cursor_size_fmt[16];
		snprintf(cursor_size_fmt, sizeof(cursor_size_fmt), "%u", cursor_size);
		setenv("XCURSOR_SIZE", cursor_size_fmt, 1);
		if (cursor_theme != NULL) {
			setenv("XCURSOR_THEME", cursor_theme, 1);
		}

#if HAVE_XWAYLAND
		if (server.xwayland.wlr_xwayland && (!server.xwayland.xcursor_manager ||
				!xcursor_manager_is_named(server.xwayland.xcursor_manager,
					cursor_theme) ||
				server.xwayland.xcursor_manager->size != cursor_size)) {

			wlr_xcursor_manager_destroy(server.xwayland.xcursor_manager);

			server.xwayland.xcursor_manager =
				wlr_xcursor_manager_create(cursor_theme, cursor_size);
			sway_assert(server.xwayland.xcursor_manager,
						"Cannot create XCursor manager for theme");

			wlr_xcursor_manager_load(server.xwayland.xcursor_manager, 1);
			struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
				server.xwayland.xcursor_manager, "left_ptr", 1);
			if (xcursor != NULL) {
				struct wlr_xcursor_image *image = xcursor->images[0];
				wlr_xwayland_set_cursor(
					server.xwayland.wlr_xwayland, image->buffer,
					image->width * 4, image->width, image->height,
					image->hotspot_x, image->hotspot_y);
			}
		}
#endif
	}

	/* Create xcursor manager if we don't have one already, or if the
	 * theme has changed */
	if (!seat->cursor->xcursor_manager ||
			!xcursor_manager_is_named(
				seat->cursor->xcursor_manager, cursor_theme) ||
			seat->cursor->xcursor_manager->size != cursor_size) {

		wlr_xcursor_manager_destroy(seat->cursor->xcursor_manager);
		seat->cursor->xcursor_manager =
			wlr_xcursor_manager_create(cursor_theme, cursor_size);
		if (!seat->cursor->xcursor_manager) {
			sway_log(SWAY_ERROR,
				"Cannot create XCursor manager for theme '%s'", cursor_theme);
		}
	}

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *sway_output = root->outputs->items[i];
		struct wlr_output *output = sway_output->wlr_output;
		bool result =
			wlr_xcursor_manager_load(seat->cursor->xcursor_manager,
				output->scale);
		if (!result) {
			sway_log(SWAY_ERROR,
				"Cannot load xcursor theme for output '%s' with scale %f",
				output->name, output->scale);
		}
	}

	// Reset the cursor so that we apply it to outputs that just appeared
	cursor_set_image(seat->cursor, NULL, NULL);
	cursor_set_image(seat->cursor, "left_ptr", NULL);
	wlr_cursor_warp(seat->cursor->cursor, NULL, seat->cursor->cursor->x,
		seat->cursor->cursor->y);
}

bool seat_is_input_allowed(struct sway_seat *seat,
		struct wlr_surface *surface) {
	struct wl_client *client = wl_resource_get_client(surface->resource);
	return !seat->exclusive_client || seat->exclusive_client == client;
}

static void send_unfocus(struct sway_container *con, void *data) {
	if (con->view) {
		view_set_activated(con->view, false);
	}
}

// Unfocus the container and any children (eg. when leaving `focus parent`)
static void seat_send_unfocus(struct sway_node *node, struct sway_seat *seat) {
	sway_cursor_constrain(seat->cursor, NULL);
	wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
	if (node->type == N_WORKSPACE) {
		workspace_for_each_container(node->sway_workspace, send_unfocus, seat);
	} else {
		send_unfocus(node->sway_container, seat);
		container_for_each_child(node->sway_container, send_unfocus, seat);
	}
}

static int handle_urgent_timeout(void *data) {
	struct sway_view *view = data;
	view_set_urgent(view, false);
	return 0;
}

static void set_workspace(struct sway_seat *seat,
		struct sway_workspace *new_ws) {
	if (seat->workspace == new_ws) {
		return;
	}

	if (seat->workspace) {
		free(seat->prev_workspace_name);
		seat->prev_workspace_name = strdup(seat->workspace->name);
		if (!seat->prev_workspace_name) {
			sway_log(SWAY_ERROR, "Unable to allocate previous workspace name");
		}
	}

	ipc_event_workspace(seat->workspace, new_ws, "focus");
	seat->workspace = new_ws;
}

void seat_set_raw_focus(struct sway_seat *seat, struct sway_node *node) {
	struct sway_seat_node *seat_node = seat_node_from_node(seat, node);
	wl_list_remove(&seat_node->link);
	wl_list_insert(&seat->focus_stack, &seat_node->link);
	node_set_dirty(node);

	// If focusing a scratchpad container that is fullscreen global, parent
	// will be NULL
	struct sway_node *parent = node_get_parent(node);
	if (parent) {
		node_set_dirty(parent);
	}
}

void seat_set_focus(struct sway_seat *seat, struct sway_node *node) {
	if (seat->focused_layer) {
		struct wlr_layer_surface_v1 *layer = seat->focused_layer;
		seat_set_focus_layer(seat, NULL);
		seat_set_focus(seat, node);
		seat_set_focus_layer(seat, layer);
		return;
	}

	struct sway_node *last_focus = seat_get_focus(seat);
	if (last_focus == node) {
		return;
	}

	struct sway_workspace *last_workspace = seat_get_focused_workspace(seat);

	if (node == NULL) {
		// Close any popups on the old focus
		if (node_is_view(last_focus)) {
			view_close_popups(last_focus->sway_container->view);
		}
		seat_send_unfocus(last_focus, seat);
		sway_input_method_relay_set_focus(&seat->im_relay, NULL);
		seat->has_focus = false;
		return;
	}

	struct sway_workspace *new_workspace = node->type == N_WORKSPACE ?
		node->sway_workspace : node->sway_container->pending.workspace;
	struct sway_container *container = node->type == N_CONTAINER ?
		node->sway_container : NULL;

	// Deny setting focus to a view which is hidden by a fullscreen container or global
	if (container && container_obstructing_fullscreen_container(container)) {
		return;
	}

	// Deny setting focus to a workspace node when using fullscreen global
	if (root->fullscreen_global && !container && new_workspace) {
		return;
	}

	struct sway_output *new_output =
		new_workspace ? new_workspace->output : NULL;

	if (last_workspace != new_workspace && new_output) {
		node_set_dirty(&new_output->node);
	}

	// find new output's old workspace, which might have to be removed if empty
	struct sway_workspace *new_output_last_ws =
		new_output ? output_get_active_workspace(new_output) : NULL;

	// Unfocus the previous focus
	if (last_focus) {
		seat_send_unfocus(last_focus, seat);
		node_set_dirty(last_focus);
		struct sway_node *parent = node_get_parent(last_focus);
		if (parent) {
			node_set_dirty(parent);
		}
	}

	// Put the container parents on the focus stack, then the workspace, then
	// the focused container.
	if (container) {
		struct sway_container *parent = container->pending.parent;
		while (parent) {
			seat_set_raw_focus(seat, &parent->node);
			parent = parent->pending.parent;
		}
	}
	if (new_workspace) {
		seat_set_raw_focus(seat, &new_workspace->node);
	}
	if (container) {
		seat_set_raw_focus(seat, &container->node);
		seat_send_focus(&container->node, seat);
	}

	// emit ipc events
	set_workspace(seat, new_workspace);
	if (container && container->view) {
		ipc_event_window(container, "focus");
	}

	// Move sticky containers to new workspace
	if (new_workspace && new_output_last_ws
			&& new_workspace != new_output_last_ws) {
		for (int i = 0; i < new_output_last_ws->floating->length; ++i) {
			struct sway_container *floater =
				new_output_last_ws->floating->items[i];
			if (container_is_sticky(floater)) {
				container_detach(floater);
				workspace_add_floating(new_workspace, floater);
				--i;
			}
		}
	}

	// Close any popups on the old focus
	if (last_focus && node_is_view(last_focus)) {
		view_close_popups(last_focus->sway_container->view);
	}

	// If urgent, either unset the urgency or start a timer to unset it
	if (container && container->view && view_is_urgent(container->view) &&
			!container->view->urgent_timer) {
		struct sway_view *view = container->view;
		if (last_workspace && last_workspace != new_workspace &&
				config->urgent_timeout > 0) {
			view->urgent_timer = wl_event_loop_add_timer(server.wl_event_loop,
					handle_urgent_timeout, view);
			if (view->urgent_timer) {
				wl_event_source_timer_update(view->urgent_timer,
						config->urgent_timeout);
			} else {
				sway_log_errno(SWAY_ERROR, "Unable to create urgency timer");
				handle_urgent_timeout(view);
			}
		} else {
			view_set_urgent(view, false);
		}
	}

	if (new_output_last_ws) {
		workspace_consider_destroy(new_output_last_ws);
	}
	if (last_workspace && last_workspace != new_output_last_ws) {
		workspace_consider_destroy(last_workspace);
	}

	seat->has_focus = true;

	if (config->smart_gaps && new_workspace) {
		// When smart gaps is on, gaps may change when the focus changes so
		// the workspace needs to be arranged
		arrange_workspace(new_workspace);
	}
}

void seat_set_focus_container(struct sway_seat *seat,
		struct sway_container *con) {
	seat_set_focus(seat, con ? &con->node : NULL);
}

void seat_set_focus_workspace(struct sway_seat *seat,
		struct sway_workspace *ws) {
	seat_set_focus(seat, ws ? &ws->node : NULL);
}

void seat_set_focus_surface(struct sway_seat *seat,
		struct wlr_surface *surface, bool unfocus) {
	if (seat->has_focus && unfocus) {
		struct sway_node *focus = seat_get_focus(seat);
		seat_send_unfocus(focus, seat);
		seat->has_focus = false;
	}

	if (surface) {
		seat_keyboard_notify_enter(seat, surface);
	} else {
		wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
	}

	sway_input_method_relay_set_focus(&seat->im_relay, surface);
	seat_tablet_pads_notify_enter(seat, surface);
}

void seat_set_focus_layer(struct sway_seat *seat,
		struct wlr_layer_surface_v1 *layer) {
	if (!layer && seat->focused_layer) {
		seat->focused_layer = NULL;
		struct sway_node *previous = seat_get_focus_inactive(seat, &root->node);
		if (previous) {
			// Hack to get seat to re-focus the return value of get_focus
			seat_set_focus(seat, NULL);
			seat_set_focus(seat, previous);
		}
		return;
	} else if (!layer || seat->focused_layer == layer) {
		return;
	}
	assert(layer->mapped);
	seat_set_focus_surface(seat, layer->surface, true);
	if (layer->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
		seat->focused_layer = layer;
	}
}

void seat_set_exclusive_client(struct sway_seat *seat,
		struct wl_client *client) {
	if (!client) {
		seat->exclusive_client = client;
		// Triggers a refocus of the topmost surface layer if necessary
		// TODO: Make layer surface focus per-output based on cursor position
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			arrange_layers(output);
		}
		return;
	}
	if (seat->focused_layer) {
		if (wl_resource_get_client(seat->focused_layer->resource) != client) {
			seat_set_focus_layer(seat, NULL);
		}
	}
	if (seat->has_focus) {
		struct sway_node *focus = seat_get_focus(seat);
		if (node_is_view(focus) && wl_resource_get_client(
					focus->sway_container->view->surface->resource) != client) {
			seat_set_focus(seat, NULL);
		}
	}
	if (seat->wlr_seat->pointer_state.focused_client) {
		if (seat->wlr_seat->pointer_state.focused_client->client != client) {
			wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
		}
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	struct wlr_touch_point *point;
	wl_list_for_each(point, &seat->wlr_seat->touch_state.touch_points, link) {
		if (point->client->client != client) {
			wlr_seat_touch_point_clear_focus(seat->wlr_seat,
					now.tv_nsec / 1000, point->touch_id);
		}
	}
	seat->exclusive_client = client;
}

struct sway_node *seat_get_focus_inactive(struct sway_seat *seat,
		struct sway_node *node) {
	if (node_is_view(node)) {
		return node;
	}
	struct sway_seat_node *current;
	wl_list_for_each(current, &seat->focus_stack, link) {
		if (node_has_ancestor(current->node, node)) {
			return current->node;
		}
	}
	if (node->type == N_WORKSPACE) {
		return node;
	}
	return NULL;
}

struct sway_container *seat_get_focus_inactive_tiling(struct sway_seat *seat,
		struct sway_workspace *workspace) {
	if (!workspace->tiling->length) {
		return NULL;
	}
	struct sway_seat_node *current;
	wl_list_for_each(current, &seat->focus_stack, link) {
		struct sway_node *node = current->node;
		if (node->type == N_CONTAINER &&
				!container_is_floating_or_child(node->sway_container) &&
				node->sway_container->pending.workspace == workspace) {
			return node->sway_container;
		}
	}
	return NULL;
}

struct sway_container *seat_get_focus_inactive_floating(struct sway_seat *seat,
		struct sway_workspace *workspace) {
	if (!workspace->floating->length) {
		return NULL;
	}
	struct sway_seat_node *current;
	wl_list_for_each(current, &seat->focus_stack, link) {
		struct sway_node *node = current->node;
		if (node->type == N_CONTAINER &&
				container_is_floating_or_child(node->sway_container) &&
				node->sway_container->pending.workspace == workspace) {
			return node->sway_container;
		}
	}
	return NULL;
}

struct sway_node *seat_get_active_tiling_child(struct sway_seat *seat,
		struct sway_node *parent) {
	if (node_is_view(parent)) {
		return parent;
	}
	struct sway_seat_node *current;
	wl_list_for_each(current, &seat->focus_stack, link) {
		struct sway_node *node = current->node;
		if (node_get_parent(node) != parent) {
			continue;
		}
		if (parent->type == N_WORKSPACE) {
			// Only consider tiling children
			struct sway_workspace *ws = parent->sway_workspace;
			if (list_find(ws->tiling, node->sway_container) == -1) {
				continue;
			}
		}
		return node;
	}
	return NULL;
}

struct sway_node *seat_get_focus(struct sway_seat *seat) {
	if (!seat->has_focus) {
		return NULL;
	}
	if (wl_list_empty(&seat->focus_stack)) {
		return NULL;
	}
	struct sway_seat_node *current =
		wl_container_of(seat->focus_stack.next, current, link);
	return current->node;
}

struct sway_workspace *seat_get_focused_workspace(struct sway_seat *seat) {
	struct sway_node *focus = seat_get_focus_inactive(seat, &root->node);
	if (!focus) {
		return NULL;
	}
	if (focus->type == N_CONTAINER) {
		return focus->sway_container->pending.workspace;
	}
	if (focus->type == N_WORKSPACE) {
		return focus->sway_workspace;
	}
	return NULL; // output doesn't have a workspace yet
}

struct sway_workspace *seat_get_last_known_workspace(struct sway_seat *seat) {
	struct sway_seat_node *current;
	wl_list_for_each(current, &seat->focus_stack, link) {
		struct sway_node *node = current->node;
		if (node->type == N_CONTAINER &&
				node->sway_container->pending.workspace) {
			return node->sway_container->pending.workspace;
		} else if (node->type == N_WORKSPACE) {
			return node->sway_workspace;
		}
	}
	return NULL;
}

struct sway_container *seat_get_focused_container(struct sway_seat *seat) {
	struct sway_node *focus = seat_get_focus(seat);
	if (focus && focus->type == N_CONTAINER) {
		return focus->sway_container;
	}
	return NULL;
}

void seat_apply_config(struct sway_seat *seat,
		struct seat_config *seat_config) {
	struct sway_seat_device *seat_device = NULL;

	if (!seat_config) {
		return;
	}

	seat->idle_inhibit_sources = seat_config->idle_inhibit_sources;
	seat->idle_wake_sources = seat_config->idle_wake_sources;

	wl_list_for_each(seat_device, &seat->devices, link) {
		seat_configure_device(seat, seat_device->input_device);
		cursor_handle_activity_from_device(seat->cursor,
			seat_device->input_device->wlr_device);
	}
}

struct seat_config *seat_get_config(struct sway_seat *seat) {
	struct seat_config *seat_config = NULL;
	for (int i = 0; i < config->seat_configs->length; ++i ) {
		seat_config = config->seat_configs->items[i];
		if (strcmp(seat->wlr_seat->name, seat_config->name) == 0) {
			return seat_config;
		}
	}

	return NULL;
}

struct seat_config *seat_get_config_by_name(const char *name) {
	struct seat_config *seat_config = NULL;
	for (int i = 0; i < config->seat_configs->length; ++i ) {
		seat_config = config->seat_configs->items[i];
		if (strcmp(name, seat_config->name) == 0) {
			return seat_config;
		}
	}

	return NULL;
}

void seat_pointer_notify_button(struct sway_seat *seat, uint32_t time_msec,
		uint32_t button, enum wlr_button_state state) {
	seat->last_button_serial = wlr_seat_pointer_notify_button(seat->wlr_seat,
			time_msec, button, state);
}

void seat_consider_warp_to_focus(struct sway_seat *seat) {
	struct sway_node *focus = seat_get_focus(seat);
	if (config->mouse_warping == WARP_NO || !focus) {
		return;
	}
	if (config->mouse_warping == WARP_OUTPUT) {
		struct sway_output *output = node_get_output(focus);
		if (output) {
			struct wlr_box box;
			output_get_box(output, &box);
			if (wlr_box_contains_point(&box,
						seat->cursor->cursor->x, seat->cursor->cursor->y)) {
				return;
			}
		}
	}

	if (focus->type == N_CONTAINER) {
		cursor_warp_to_container(seat->cursor, focus->sway_container, false);
	} else {
		cursor_warp_to_workspace(seat->cursor, focus->sway_workspace);
	}
}

void seatop_unref(struct sway_seat *seat, struct sway_container *con) {
	if (seat->seatop_impl->unref) {
		seat->seatop_impl->unref(seat, con);
	}
}

void seatop_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	if (seat->seatop_impl->button) {
		seat->seatop_impl->button(seat, time_msec, device, button, state);
	}
}

void seatop_pointer_motion(struct sway_seat *seat, uint32_t time_msec) {
	if (seat->seatop_impl->pointer_motion) {
		seat->seatop_impl->pointer_motion(seat, time_msec);
	}
}

void seatop_pointer_axis(struct sway_seat *seat,
		struct wlr_event_pointer_axis *event) {
	if (seat->seatop_impl->pointer_axis) {
		seat->seatop_impl->pointer_axis(seat, event);
	}
}

void seatop_tablet_tool_tip(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (seat->seatop_impl->tablet_tool_tip) {
		seat->seatop_impl->tablet_tool_tip(seat, tool, time_msec, state);
	}
}

void seatop_tablet_tool_motion(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec) {
	if (seat->seatop_impl->tablet_tool_motion) {
		seat->seatop_impl->tablet_tool_motion(seat, tool, time_msec);
	} else {
		seatop_pointer_motion(seat, time_msec);
	}
}

void seatop_rebase(struct sway_seat *seat, uint32_t time_msec) {
	if (seat->seatop_impl->rebase) {
		seat->seatop_impl->rebase(seat, time_msec);
	}
}

void seatop_end(struct sway_seat *seat) {
	if (seat->seatop_impl && seat->seatop_impl->end) {
		seat->seatop_impl->end(seat);
	}
	free(seat->seatop_data);
	seat->seatop_data = NULL;
	seat->seatop_impl = NULL;
}

void seatop_render(struct sway_seat *seat, struct sway_output *output,
		pixman_region32_t *damage) {
	if (seat->seatop_impl->render) {
		seat->seatop_impl->render(seat, output, damage);
	}
}

bool seatop_allows_set_cursor(struct sway_seat *seat) {
	return seat->seatop_impl->allow_set_cursor;
}

struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(
		const struct sway_seat *seat,
		const struct wlr_surface *surface) {
	struct sway_keyboard_shortcuts_inhibitor *sway_inhibitor = NULL;
	wl_list_for_each(sway_inhibitor, &seat->keyboard_shortcuts_inhibitors, link) {
		if (sway_inhibitor->inhibitor->surface == surface) {
			return sway_inhibitor;
		}
	}

	return NULL;
}

struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(
		const struct sway_seat *seat) {
	return keyboard_shortcuts_inhibitor_get_for_surface(seat,
		seat->wlr_seat->keyboard_state.focused_surface);
}
