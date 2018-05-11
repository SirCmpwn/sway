#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <strings.h>
#include <time.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "sway/debug.h"
#include "sway/tree/container.h"
#include "sway/tree/workspace.h"
#include "sway/input/seat.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/input/keyboard.h"
#include "sway/ipc-server.h"
#include "sway/layers.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "log.h"

static void seat_device_destroy(struct sway_seat_device *seat_device) {
	if (!seat_device) {
		return;
	}

	sway_keyboard_destroy(seat_device->keyboard);
	wlr_cursor_detach_input_device(seat_device->sway_seat->cursor->cursor,
		seat_device->input_device->wlr_device);
	wl_list_remove(&seat_device->link);
	free(seat_device);
}

void seat_destroy(struct sway_seat *seat) {
	struct sway_seat_device *seat_device, *next;
	wl_list_for_each_safe(seat_device, next, &seat->devices, link) {
		seat_device_destroy(seat_device);
	}
	sway_cursor_destroy(seat->cursor);
	wl_list_remove(&seat->link);
	wlr_seat_destroy(seat->wlr_seat);
}

static struct sway_seat_container *seat_container_from_container(
		struct sway_seat *seat, struct sway_container *con);

static void seat_container_destroy(struct sway_seat_container *seat_con) {
	struct sway_container *con = seat_con->container;
	struct sway_container *child = NULL;

	if (con->children != NULL) {
		for (int i = 0; i < con->children->length; ++i) {
			child = con->children->items[i];
			struct sway_seat_container *seat_child =
				seat_container_from_container(seat_con->seat, child);
			seat_container_destroy(seat_child);
		}
	}

	wl_list_remove(&seat_con->destroy.link);
	wl_list_remove(&seat_con->link);
	free(seat_con);
}

/**
 * Activate all views within this container recursively.
 */
static void seat_send_activate(struct sway_container *con,
		struct sway_seat *seat) {
	if (con->type == C_VIEW) {
		if (!seat_is_input_allowed(seat, con->sway_view->surface)) {
			wlr_log(L_DEBUG, "Refusing to set focus, input is inhibited");
			return;
		}
		view_set_activated(con->sway_view, true);
	} else {
		for (int i = 0; i < con->children->length; ++i) {
			struct sway_container *child = con->children->items[i];
			seat_send_activate(child, seat);
		}
	}
}

/**
 * If con is a view, set it as active and enable keyboard input.
 * If con is a container, set all child views as active and don't enable
 * keyboard input on any.
 */
static void seat_send_focus(struct sway_container *con,
		struct sway_seat *seat) {
	seat_send_activate(con, seat);

	if (con->type == C_VIEW
			&& seat_is_input_allowed(seat, con->sway_view->surface)) {
		if (con->sway_view->type == SWAY_VIEW_XWAYLAND) {
			struct wlr_xwayland *xwayland = seat->input->server->xwayland;
			wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
		}
		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
		if (keyboard) {
			wlr_seat_keyboard_notify_enter(seat->wlr_seat,
					con->sway_view->surface, keyboard->keycodes,
					keyboard->num_keycodes, &keyboard->modifiers);
		} else {
			wlr_seat_keyboard_notify_enter(
					seat->wlr_seat, con->sway_view->surface, NULL, 0, NULL);
		}
	}
}

static struct sway_container *seat_get_focus_by_type(struct sway_seat *seat,
		struct sway_container *container, enum sway_container_type type) {
	if (container->type == C_VIEW || container->children->length == 0) {
		return container;
	}

	struct sway_seat_container *current = NULL;
	wl_list_for_each(current, &seat->focus_stack, link) {
		if (current->container->type != type && type != C_TYPES) {
			continue;
		}

		if (container_has_child(container, current->container)) {
			return current->container;
		}
	}

	return NULL;
}

void seat_focus_inactive_children_for_each(struct sway_seat *seat,
		struct sway_container *container,
		void (*f)(struct sway_container *container, void *data), void *data) {
	struct sway_seat_container *current = NULL;
	wl_list_for_each(current, &seat->focus_stack, link) {
		if (current->container->parent == NULL) {
			continue;
		}
		if (current->container->parent == container) {
			f(current->container, data);
		}
	}
}

struct sway_container *seat_get_focus_inactive_view(struct sway_seat *seat,
		struct sway_container *container) {
	return seat_get_focus_by_type(seat, container, C_VIEW);
}

static void handle_seat_container_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_seat_container *seat_con =
		wl_container_of(listener, seat_con, destroy);
	struct sway_seat *seat = seat_con->seat;
	struct sway_container *con = seat_con->container;
	struct sway_container *parent = con->parent;
	struct sway_container *focus = seat_get_focus(seat);

	bool set_focus =
		focus != NULL &&
		(focus == con || container_has_child(con, focus)) &&
		con->type != C_WORKSPACE;

	seat_container_destroy(seat_con);

	if (set_focus) {
		struct sway_container *next_focus = NULL;
		while (next_focus == NULL) {
			next_focus = seat_get_focus_by_type(seat, parent, C_VIEW);

			if (next_focus == NULL && parent->type == C_WORKSPACE) {
				next_focus = parent;
				break;
			}

			parent = parent->parent;
		}

		// the structure change might have caused it to move up to the top of
		// the focus stack without sending focus notifications to the view
		if (seat_get_focus(seat) == next_focus) {
			seat_send_focus(next_focus, seat);
		} else {
			seat_set_focus(seat, next_focus);
		}
	}
}

static struct sway_seat_container *seat_container_from_container(
		struct sway_seat *seat, struct sway_container *con) {
	if (con->type == C_ROOT || con->type == C_OUTPUT) {
		// these don't get seat containers ever
		return NULL;
	}

	struct sway_seat_container *seat_con = NULL;
	wl_list_for_each(seat_con, &seat->focus_stack, link) {
		if (seat_con->container == con) {
			return seat_con;
		}
	}

	seat_con = calloc(1, sizeof(struct sway_seat_container));
	if (seat_con == NULL) {
		wlr_log(L_ERROR, "could not allocate seat container");
		return NULL;
	}

	seat_con->container = con;
	seat_con->seat = seat;
	wl_list_insert(seat->focus_stack.prev, &seat_con->link);
	wl_signal_add(&con->events.destroy, &seat_con->destroy);
	seat_con->destroy.notify = handle_seat_container_destroy;

	return seat_con;
}

static void handle_new_container(struct wl_listener *listener, void *data) {
	struct sway_seat *seat = wl_container_of(listener, seat, new_container);
	struct sway_container *con = data;
	seat_container_from_container(seat, con);
}

static void collect_focus_iter(struct sway_container *con, void *data) {
	struct sway_seat *seat = data;
	if (con->type > C_WORKSPACE) {
		return;
	}
	struct sway_seat_container *seat_con =
		seat_container_from_container(seat, con);
	if (!seat_con) {
		return;
	}
	wl_list_remove(&seat_con->link);
	wl_list_insert(&seat->focus_stack, &seat_con->link);
}

struct sway_seat *seat_create(struct sway_input_manager *input,
		const char *seat_name) {
	struct sway_seat *seat = calloc(1, sizeof(struct sway_seat));
	if (!seat) {
		return NULL;
	}

	seat->wlr_seat = wlr_seat_create(input->server->wl_display, seat_name);
	if (!sway_assert(seat->wlr_seat, "could not allocate seat")) {
		free(seat);
		return NULL;
	}

	seat->cursor = sway_cursor_create(seat);
	if (!seat->cursor) {
		wlr_seat_destroy(seat->wlr_seat);
		free(seat);
		return NULL;
	}

	// init the focus stack
	wl_list_init(&seat->focus_stack);

	container_for_each_descendant_dfs(&root_container,
		collect_focus_iter, seat);

	wl_signal_add(&root_container.sway_root->events.new_container,
		&seat->new_container);
	seat->new_container.notify = handle_new_container;

	seat->input = input;
	wl_list_init(&seat->devices);

	wlr_seat_set_capabilities(seat->wlr_seat,
		WL_SEAT_CAPABILITY_KEYBOARD |
		WL_SEAT_CAPABILITY_POINTER |
		WL_SEAT_CAPABILITY_TOUCH);

	seat_configure_xcursor(seat);

	wl_list_insert(&input->seats, &seat->link);

	return seat;
}

static void seat_apply_input_config(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	const char *mapped_to_output = NULL;

	struct input_config *ic = input_device_get_config(
			sway_device->input_device);
	if (ic != NULL) {
		wlr_log(L_DEBUG, "Applying input config to %s",
			sway_device->input_device->identifier);

		mapped_to_output = ic->mapped_to_output;
	}

	if (mapped_to_output == NULL) {
		mapped_to_output = sway_device->input_device->wlr_device->output_name;
	}
	if (mapped_to_output != NULL) {
		wlr_log(L_DEBUG, "Mapping input device %s to output %s",
			sway_device->input_device->identifier, mapped_to_output);
		struct sway_container *output = NULL;
		for (int i = 0; i < root_container.children->length; ++i) {
			struct sway_container *_output = root_container.children->items[i];
			if (strcasecmp(_output->name, mapped_to_output) == 0) {
				output = _output;
				break;
			}
		}
		if (output) {
			wlr_cursor_map_input_to_output(seat->cursor->cursor,
				sway_device->input_device->wlr_device,
				output->sway_output->wlr_output);
			wlr_log(L_DEBUG, "Mapped to output %s", output->name);
		}
	}
}

static void seat_configure_pointer(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		sway_device->input_device->wlr_device);
	seat_apply_input_config(seat, sway_device);
}

static void seat_configure_keyboard(struct sway_seat *seat,
		struct sway_seat_device *seat_device) {
	if (!seat_device->keyboard) {
		sway_keyboard_create(seat, seat_device);
	}
	struct wlr_keyboard *wlr_keyboard =
		seat_device->input_device->wlr_device->keyboard;
	sway_keyboard_configure(seat_device->keyboard);
	wlr_seat_set_keyboard(seat->wlr_seat,
			seat_device->input_device->wlr_device);
	struct sway_container *focus = seat_get_focus(seat);
	if (focus && focus->type == C_VIEW) {
		// force notify reenter to pick up the new configuration
		wlr_seat_keyboard_clear_focus(seat->wlr_seat);
		wlr_seat_keyboard_notify_enter(seat->wlr_seat,
				focus->sway_view->surface, wlr_keyboard->keycodes,
				wlr_keyboard->num_keycodes, &wlr_keyboard->modifiers);
	}
}

static void seat_configure_touch(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		sway_device->input_device->wlr_device);
	seat_apply_input_config(seat, sway_device);
}

static void seat_configure_tablet_tool(struct sway_seat *seat,
		struct sway_seat_device *sway_device) {
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		sway_device->input_device->wlr_device);
	seat_apply_input_config(seat, sway_device);
}

static struct sway_seat_device *seat_get_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	struct sway_seat_device *seat_device = NULL;
	wl_list_for_each(seat_device, &seat->devices, link) {
		if (seat_device->input_device == input_device) {
			return seat_device;
		}
	}

	return NULL;
}

void seat_configure_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	struct sway_seat_device *seat_device =
		seat_get_device(seat, input_device);
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
		case WLR_INPUT_DEVICE_TOUCH:
			seat_configure_touch(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			seat_configure_tablet_tool(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			wlr_log(L_DEBUG, "TODO: configure tablet pad");
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
		wlr_log(L_DEBUG, "could not allocate seat device");
		return;
	}

	wlr_log(L_DEBUG, "adding device %s to seat %s",
		input_device->identifier, seat->wlr_seat->name);

	seat_device->sway_seat = seat;
	seat_device->input_device = input_device;
	wl_list_insert(&seat->devices, &seat_device->link);

	seat_configure_device(seat, input_device);
}

void seat_remove_device(struct sway_seat *seat,
		struct sway_input_device *input_device) {
	struct sway_seat_device *seat_device =
		seat_get_device(seat, input_device);

	if (!seat_device) {
		return;
	}

	wlr_log(L_DEBUG, "removing device %s from seat %s",
		input_device->identifier, seat->wlr_seat->name);

	seat_device_destroy(seat_device);
}

void seat_configure_xcursor(struct sway_seat *seat) {
	// TODO configure theme and size
	const char *cursor_theme = NULL;

	if (!seat->cursor->xcursor_manager) {
		seat->cursor->xcursor_manager =
			wlr_xcursor_manager_create(cursor_theme, 24);
		if (sway_assert(seat->cursor->xcursor_manager,
					"Cannot create XCursor manager for theme %s",
					cursor_theme)) {
			return;
		}
	}

	for (int i = 0; i < root_container.children->length; ++i) {
		struct sway_container *output_container =
			root_container.children->items[i];
		struct wlr_output *output =
			output_container->sway_output->wlr_output;
		bool result =
			wlr_xcursor_manager_load(seat->cursor->xcursor_manager,
				output->scale);

		sway_assert(!result,
			"Cannot load xcursor theme for output '%s' with scale %f",
			// TODO: Fractional scaling
			output->name, (double)output->scale);
	}

	wlr_xcursor_manager_set_cursor_image(seat->cursor->xcursor_manager,
		"left_ptr", seat->cursor->cursor);
	wlr_cursor_warp(seat->cursor->cursor, NULL, seat->cursor->cursor->x,
		seat->cursor->cursor->y);
}

bool seat_is_input_allowed(struct sway_seat *seat,
		struct wlr_surface *surface) {
	struct wl_client *client = wl_resource_get_client(surface->resource);
	return !seat->exclusive_client || seat->exclusive_client == client;
}

// Unfocus the container and any children (eg. when leaving `focus parent`)
static void seat_send_unfocus(struct sway_container *container,
		struct sway_seat *seat) {
	if (container->type == C_VIEW) {
		wlr_seat_keyboard_clear_focus(seat->wlr_seat);
		view_set_activated(container->sway_view, false);
	} else {
		for (int i = 0; i < container->children->length; ++i) {
			struct sway_container *child = container->children->items[i];
			seat_send_unfocus(child, seat);
		}
	}
}

void seat_set_focus_warp(struct sway_seat *seat,
		struct sway_container *container, bool warp) {
	if (seat->focused_layer) {
		return;
	}

	struct sway_container *last_focus = seat_get_focus(seat);
	if (container && last_focus == container) {
		return;
	}

	struct sway_container *last_workspace = last_focus;
	if (last_workspace && last_workspace->type != C_WORKSPACE) {
		last_workspace = container_parent(last_workspace, C_WORKSPACE);
	}
	struct sway_container *new_workspace = container;
	if (new_workspace && new_workspace->type != C_WORKSPACE) {
		new_workspace = container_parent(new_workspace, C_WORKSPACE);
	}

	if (last_workspace && last_workspace == new_workspace
			&& last_workspace->sway_workspace->fullscreen
			&& !container->sway_view->is_fullscreen) {
		return;
	}

	struct sway_container *last_output = last_focus;
	if (last_output && last_output->type != C_OUTPUT) {
		last_output = container_parent(last_output, C_OUTPUT);
	}
	struct sway_container *new_output = container;
	if (new_output && new_output->type != C_OUTPUT) {
		new_output = container_parent(new_output, C_OUTPUT);
	}

	// find new output's old workspace, which might have to be removed if empty
	struct sway_container *new_output_last_ws = NULL;
	if (last_output && new_output && last_output != new_output) {
		new_output_last_ws =
			seat_get_focus_by_type(seat, new_output, C_WORKSPACE);
	}

	if (container && container->parent) {
		struct sway_seat_container *seat_con =
			seat_container_from_container(seat, container);
		if (seat_con == NULL) {
			return;
		}

		// put all the anscestors of this container on top of the focus stack
		struct sway_seat_container *parent =
			seat_container_from_container(seat, container->parent);
		while (parent) {
			wl_list_remove(&parent->link);
			wl_list_insert(&seat->focus_stack, &parent->link);

			parent =
				seat_container_from_container(seat,
					parent->container->parent);
		}

		wl_list_remove(&seat_con->link);
		wl_list_insert(&seat->focus_stack, &seat_con->link);

		if (last_focus) {
			seat_send_unfocus(last_focus, seat);
		}

		seat_send_focus(container, seat);
		container_damage_whole(container);
	}

	// clean up unfocused empty workspace on new output
	if (new_output_last_ws) {
		if (!workspace_is_visible(new_output_last_ws)
				&& new_output_last_ws->children->length == 0) {
			if (last_workspace == new_output_last_ws) {
				last_focus = NULL;
				last_workspace = NULL;
			}
			container_destroy(new_output_last_ws);
		}
	}

	if (last_focus) {
		if (last_workspace) {
			ipc_event_workspace(last_workspace, container, "focus");
			if (!workspace_is_visible(last_workspace)
					&& last_workspace->children->length == 0) {
				if (last_workspace == last_focus) {
					last_focus = NULL;
				}
				container_destroy(last_workspace);
			}
		}

		if (config->mouse_warping && warp) {
			if (new_output && last_output && new_output != last_output) {
				double x = new_output->x + container->x +
					container->width / 2.0;
				double y = new_output->y + container->y +
					container->height / 2.0;
				struct wlr_output *wlr_output =
					new_output->sway_output->wlr_output;
				if (!wlr_output_layout_contains_point(
						root_container.sway_root->output_layout,
						wlr_output, seat->cursor->cursor->x,
						seat->cursor->cursor->y)) {
					wlr_cursor_warp(seat->cursor->cursor, NULL, x, y);
					cursor_send_pointer_motion(seat->cursor, 0);
				}
			}
		}
	}

	if (last_focus) {
		container_damage_whole(last_focus);
	}

	if (last_workspace && last_workspace != new_workspace) {
		cursor_send_pointer_motion(seat->cursor, 0);
	}

	seat->has_focus = (container != NULL);

	update_debug_tree();
}

void seat_set_focus(struct sway_seat *seat,
		struct sway_container *container) {
	seat_set_focus_warp(seat, container, true);
}

void seat_set_focus_surface(struct sway_seat *seat,
		struct wlr_surface *surface) {
	if (seat->focused_layer != NULL) {
		return;
	}
	if (seat->has_focus) {
		struct sway_container *focus = seat_get_focus(seat);
		seat_send_unfocus(focus, seat);
		seat->has_focus = false;
	}
	struct wlr_keyboard *keyboard =
		wlr_seat_get_keyboard(seat->wlr_seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface, NULL, 0, NULL);
	}
}

void seat_set_focus_layer(struct sway_seat *seat,
		struct wlr_layer_surface *layer) {
	if (!layer && seat->focused_layer) {
		seat->focused_layer = NULL;
		struct sway_container *previous = seat_get_focus(seat);
		if (previous) {
			wlr_log(L_DEBUG, "Returning focus to %p %s '%s'", previous,
					container_type_to_str(previous->type), previous->name);
			// Hack to get seat to re-focus the return value of get_focus
			seat_set_focus(seat, previous->parent);
			seat_set_focus(seat, previous);
		}
		return;
	} else if (!layer || seat->focused_layer == layer) {
		return;
	}
	seat_set_focus_surface(seat, layer->surface);
	if (layer->layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
		seat->focused_layer = layer;
	}
}

void seat_set_exclusive_client(struct sway_seat *seat,
		struct wl_client *client) {
	if (!client) {
		seat->exclusive_client = client;
		// Triggers a refocus of the topmost surface layer if necessary
		// TODO: Make layer surface focus per-output based on cursor position
		for (int i = 0; i < root_container.children->length; ++i) {
			struct sway_container *output = root_container.children->items[i];
			if (!sway_assert(output->type == C_OUTPUT,
						"root container has non-output child")) {
				continue;
			}
			arrange_layers(output->sway_output);
		}
		return;
	}
	if (seat->focused_layer) {
		if (wl_resource_get_client(seat->focused_layer->resource) != client) {
			seat_set_focus_layer(seat, NULL);
		}
	}
	if (seat->has_focus) {
		struct sway_container *focus = seat_get_focus(seat);
		if (focus->type == C_VIEW && wl_resource_get_client(
					focus->sway_view->surface->resource) != client) {
			seat_set_focus(seat, NULL);
		}
	}
	if (seat->wlr_seat->pointer_state.focused_client) {
		if (seat->wlr_seat->pointer_state.focused_client->client != client) {
			wlr_seat_pointer_clear_focus(seat->wlr_seat);
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

struct sway_container *seat_get_focus_inactive(struct sway_seat *seat,
		struct sway_container *container) {
	return seat_get_focus_by_type(seat, container, C_TYPES);
}

struct sway_container *sway_seat_get_focus(struct sway_seat *seat) {
	if (!seat->has_focus) {
		return NULL;
	}
	return seat_get_focus_inactive(seat, &root_container);
}

struct sway_container *seat_get_focus(struct sway_seat *seat) {
	if (!seat->has_focus) {
		return NULL;
	}
	return seat_get_focus_inactive(seat, &root_container);
}

void seat_apply_config(struct sway_seat *seat,
		struct seat_config *seat_config) {
	struct sway_seat_device *seat_device = NULL;

	if (!seat_config) {
		return;
	}

	wl_list_for_each(seat_device, &seat->devices, link) {
		seat_configure_device(seat, seat_device->input_device);
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
