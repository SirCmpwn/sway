#define _POSIX_C_SOURCE 199309L
#include <float.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include "log.h"
#include "sway/decoration.h"
#include "sway/desktop.h"
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/xdg_decoration.h"

static const struct sway_view_child_impl popup_impl;

static void popup_get_root_coords(struct sway_view_child *child,
		int *root_sx, int *root_sy) {
	struct sway_xdg_popup *popup = (struct sway_xdg_popup *)child;
	struct wlr_xdg_surface *surface = popup->wlr_xdg_surface;

	int x_offset = -child->view->geometry.x - surface->geometry.x;
	int y_offset = -child->view->geometry.y - surface->geometry.y;

	wlr_xdg_popup_get_toplevel_coords(surface->popup,
		x_offset + surface->popup->geometry.x,
		y_offset + surface->popup->geometry.y,
		root_sx, root_sy);
}

static void popup_destroy(struct sway_view_child *child) {
	if (!sway_assert(child->impl == &popup_impl,
			"Expected an xdg_shell popup")) {
		return;
	}
	struct sway_xdg_popup *popup = (struct sway_xdg_popup *)child;
	wl_list_remove(&popup->new_popup.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static const struct sway_view_child_impl popup_impl = {
	.get_root_coords = popup_get_root_coords,
	.destroy = popup_destroy,
};

static struct sway_xdg_popup *popup_create(
	struct wlr_xdg_popup *wlr_popup, struct sway_view *view);

static void popup_handle_new_popup(struct wl_listener *listener, void *data) {
	struct sway_xdg_popup *popup =
		wl_container_of(listener, popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	popup_create(wlr_popup, popup->child.view);
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_xdg_popup *popup = wl_container_of(listener, popup, destroy);
	view_child_destroy(&popup->child);
}

static void popup_unconstrain(struct sway_xdg_popup *popup) {
	struct sway_view *view = popup->child.view;
	struct wlr_xdg_popup *wlr_popup = popup->wlr_xdg_surface->popup;

	struct sway_output *output = view->container->pending.workspace->output;

	// the output box expressed in the coordinate system of the toplevel parent
	// of the popup
	struct wlr_box output_toplevel_sx_box = {
		.x = output->lx - view->container->pending.content_x,
		.y = output->ly - view->container->pending.content_y,
		.width = output->width,
		.height = output->height,
	};

	wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static struct sway_xdg_popup *popup_create(
		struct wlr_xdg_popup *wlr_popup, struct sway_view *view) {
	struct wlr_xdg_surface *xdg_surface = wlr_popup->base;

	struct sway_xdg_popup *popup =
		calloc(1, sizeof(struct sway_xdg_popup));
	if (popup == NULL) {
		return NULL;
	}
	view_child_init(&popup->child, &popup_impl, view, xdg_surface->surface);
	popup->wlr_xdg_surface = xdg_surface;

	wl_signal_add(&xdg_surface->events.new_popup, &popup->new_popup);
	popup->new_popup.notify = popup_handle_new_popup;
	wl_signal_add(&xdg_surface->events.destroy, &popup->destroy);
	popup->destroy.notify = popup_handle_destroy;

	wl_signal_add(&xdg_surface->events.map, &popup->child.surface_map);
	wl_signal_add(&xdg_surface->events.unmap, &popup->child.surface_unmap);

	popup_unconstrain(popup);

	return popup;
}


static struct sway_xdg_shell_view *xdg_shell_view_from_view(
		struct sway_view *view) {
	if (!sway_assert(view->type == SWAY_VIEW_XDG_SHELL,
			"Expected xdg_shell view")) {
		return NULL;
	}
	return (struct sway_xdg_shell_view *)view;
}

static void get_constraints(struct sway_view *view, double *min_width,
		double *max_width, double *min_height, double *max_height) {
	struct wlr_xdg_toplevel_state *state =
		&view->wlr_xdg_surface->toplevel->current;
	*min_width = state->min_width > 0 ? state->min_width : DBL_MIN;
	*max_width = state->max_width > 0 ? state->max_width : DBL_MAX;
	*min_height = state->min_height > 0 ? state->min_height : DBL_MIN;
	*max_height = state->max_height > 0 ? state->max_height : DBL_MAX;
}

static const char *get_string_prop(struct sway_view *view,
		enum sway_view_prop prop) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return NULL;
	}
	switch (prop) {
	case VIEW_PROP_TITLE:
		return view->wlr_xdg_surface->toplevel->title;
	case VIEW_PROP_APP_ID:
		return view->wlr_xdg_surface->toplevel->app_id;
	default:
		return NULL;
	}
}

static uint32_t configure(struct sway_view *view, double lx, double ly,
		int width, int height) {
	struct sway_xdg_shell_view *xdg_shell_view =
		xdg_shell_view_from_view(view);
	if (xdg_shell_view == NULL) {
		return 0;
	}
	return wlr_xdg_toplevel_set_size(view->wlr_xdg_surface, width, height);
}

static void set_activated(struct sway_view *view, bool activated) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return;
	}
	struct wlr_xdg_surface *surface = view->wlr_xdg_surface;
	if (surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_set_activated(surface, activated);
	}
}

static void set_tiled(struct sway_view *view, bool tiled) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return;
	}
	struct wlr_xdg_surface *surface = view->wlr_xdg_surface;
	enum wlr_edges edges = WLR_EDGE_NONE;
	if (tiled) {
		edges = WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP |
				WLR_EDGE_BOTTOM;
	}
	wlr_xdg_toplevel_set_tiled(surface, edges);
}

static void set_fullscreen(struct sway_view *view, bool fullscreen) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return;
	}
	struct wlr_xdg_surface *surface = view->wlr_xdg_surface;
	wlr_xdg_toplevel_set_fullscreen(surface, fullscreen);
}

static void set_resizing(struct sway_view *view, bool resizing) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return;
	}
	struct wlr_xdg_surface *surface = view->wlr_xdg_surface;
	wlr_xdg_toplevel_set_resizing(surface, resizing);
}

static bool wants_floating(struct sway_view *view) {
	struct wlr_xdg_toplevel *toplevel = view->wlr_xdg_surface->toplevel;
	struct wlr_xdg_toplevel_state *state = &toplevel->current;
	return (state->min_width != 0 && state->min_height != 0
		&& (state->min_width == state->max_width
		|| state->min_height == state->max_height))
		|| toplevel->parent;
}

static void for_each_surface(struct sway_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return;
	}
	wlr_xdg_surface_for_each_surface(view->wlr_xdg_surface, iterator,
		user_data);
}

static void for_each_popup_surface(struct sway_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return;
	}
	wlr_xdg_surface_for_each_popup_surface(view->wlr_xdg_surface, iterator,
		user_data);
}

static bool is_transient_for(struct sway_view *child,
		struct sway_view *ancestor) {
	if (xdg_shell_view_from_view(child) == NULL) {
		return false;
	}
	struct wlr_xdg_surface *surface = child->wlr_xdg_surface;
	while (surface && surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		if (surface->toplevel->parent == ancestor->wlr_xdg_surface) {
			return true;
		}
		surface = surface->toplevel->parent;
	}
	return false;
}

static void _close(struct sway_view *view) {
	if (xdg_shell_view_from_view(view) == NULL) {
		return;
	}
	struct wlr_xdg_surface *surface = view->wlr_xdg_surface;
	if (surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL
			&& surface->toplevel) {
		wlr_xdg_toplevel_send_close(surface);
	}
}

static void close_popups(struct sway_view *view) {
	struct wlr_xdg_popup *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &view->wlr_xdg_surface->popups, link) {
		wlr_xdg_popup_destroy(popup->base);
	}
}

static void destroy(struct sway_view *view) {
	struct sway_xdg_shell_view *xdg_shell_view =
		xdg_shell_view_from_view(view);
	if (xdg_shell_view == NULL) {
		return;
	}
	free(xdg_shell_view);
}

static const struct sway_view_impl view_impl = {
	.get_constraints = get_constraints,
	.get_string_prop = get_string_prop,
	.configure = configure,
	.set_activated = set_activated,
	.set_tiled = set_tiled,
	.set_fullscreen = set_fullscreen,
	.set_resizing = set_resizing,
	.wants_floating = wants_floating,
	.for_each_surface = for_each_surface,
	.for_each_popup_surface = for_each_popup_surface,
	.is_transient_for = is_transient_for,
	.close = _close,
	.close_popups = close_popups,
	.destroy = destroy,
};

static void handle_commit(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, commit);
	struct sway_view *view = &xdg_shell_view->view;
	struct wlr_xdg_surface *xdg_surface = view->wlr_xdg_surface;

	struct wlr_box new_geo;
	wlr_xdg_surface_get_geometry(xdg_surface, &new_geo);
	bool new_size = new_geo.width != view->geometry.width ||
			new_geo.height != view->geometry.height ||
			new_geo.x != view->geometry.x ||
			new_geo.y != view->geometry.y;

	if (new_size) {
		// The client changed its surface size in this commit. For floating
		// containers, we resize the container to match. For tiling containers,
		// we only recenter the surface.
		desktop_damage_view(view);
		memcpy(&view->geometry, &new_geo, sizeof(struct wlr_box));
		if (container_is_floating(view->container)) {
			view_update_size(view);
			transaction_commit_dirty_client();
		} else {
			view_center_surface(view);
		}
		desktop_damage_view(view);
	}

	if (view->container->node.instruction) {
		transaction_notify_view_ready_by_serial(view,
				xdg_surface->configure_serial);
	}

	view_damage_from(view);
}

static void handle_set_title(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, set_title);
	struct sway_view *view = &xdg_shell_view->view;
	view_update_title(view, false);
	view_execute_criteria(view);
}

static void handle_set_app_id(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, set_app_id);
	struct sway_view *view = &xdg_shell_view->view;
	view_execute_criteria(view);
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	popup_create(wlr_popup, &xdg_shell_view->view);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, request_fullscreen);
	struct wlr_xdg_toplevel_set_fullscreen_event *e = data;
	struct wlr_xdg_surface *xdg_surface =
		xdg_shell_view->view.wlr_xdg_surface;
	struct sway_view *view = &xdg_shell_view->view;

	if (!sway_assert(xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL,
				"xdg_shell requested fullscreen of surface with role %i",
				xdg_surface->role)) {
		return;
	}
	if (!xdg_surface->mapped) {
		return;
	}

	struct sway_container *container = view->container;
	if (e->fullscreen && e->output && e->output->data) {
		struct sway_output *output = e->output->data;
		struct sway_workspace *ws = output_get_active_workspace(output);
		if (ws && !container_is_scratchpad_hidden(container)) {
			if (container_is_floating(container)) {
				workspace_add_floating(ws, container);
			} else {
				container = workspace_add_tiling(ws, container);
			}
		}
	}

	container_set_fullscreen(container, e->fullscreen);

	arrange_root();
	transaction_commit_dirty();
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	struct wlr_xdg_surface *surface = data;
	wlr_xdg_surface_schedule_configure(surface);
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, request_move);
	struct sway_view *view = &xdg_shell_view->view;
	if (!container_is_floating(view->container)) {
		return;
	}
	struct wlr_xdg_toplevel_move_event *e = data;
	struct sway_seat *seat = e->seat->seat->data;
	if (e->serial == seat->last_button_serial) {
		seatop_begin_move_floating(seat, view->container);
	}
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, request_resize);
	struct sway_view *view = &xdg_shell_view->view;
	if (!container_is_floating(view->container)) {
		return;
	}
	struct wlr_xdg_toplevel_resize_event *e = data;
	struct sway_seat *seat = e->seat->seat->data;
	if (e->serial == seat->last_button_serial) {
		seatop_begin_resize_floating(seat, view->container, e->edges);
	}
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, unmap);
	struct sway_view *view = &xdg_shell_view->view;

	if (!sway_assert(view->surface, "Cannot unmap unmapped view")) {
		return;
	}

	view_unmap(view);

	wl_list_remove(&xdg_shell_view->commit.link);
	wl_list_remove(&xdg_shell_view->new_popup.link);
	wl_list_remove(&xdg_shell_view->request_fullscreen.link);
	wl_list_remove(&xdg_shell_view->request_maximize.link);
	wl_list_remove(&xdg_shell_view->request_move.link);
	wl_list_remove(&xdg_shell_view->request_resize.link);
	wl_list_remove(&xdg_shell_view->set_title.link);
	wl_list_remove(&xdg_shell_view->set_app_id.link);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, map);
	struct sway_view *view = &xdg_shell_view->view;
	struct wlr_xdg_surface *xdg_surface = view->wlr_xdg_surface;

	view->natural_width = view->wlr_xdg_surface->geometry.width;
	view->natural_height = view->wlr_xdg_surface->geometry.height;
	if (!view->natural_width && !view->natural_height) {
		view->natural_width = view->wlr_xdg_surface->surface->current.width;
		view->natural_height = view->wlr_xdg_surface->surface->current.height;
	}

	bool csd = false;

	if (!view->xdg_decoration) {
		struct sway_server_decoration *deco =
				decoration_from_surface(xdg_surface->surface);
		csd = !deco || deco->wlr_server_decoration->mode ==
			WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;

	}

	view_map(view, view->wlr_xdg_surface->surface,
		xdg_surface->toplevel->client_pending.fullscreen,
		xdg_surface->toplevel->client_pending.fullscreen_output,
		csd);

	transaction_commit_dirty();

	xdg_shell_view->commit.notify = handle_commit;
	wl_signal_add(&xdg_surface->surface->events.commit,
		&xdg_shell_view->commit);

	xdg_shell_view->new_popup.notify = handle_new_popup;
	wl_signal_add(&xdg_surface->events.new_popup,
		&xdg_shell_view->new_popup);

	xdg_shell_view->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&xdg_surface->toplevel->events.request_fullscreen,
			&xdg_shell_view->request_fullscreen);

	xdg_shell_view->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&xdg_surface->toplevel->events.request_maximize,
			&xdg_shell_view->request_maximize);

	xdg_shell_view->request_move.notify = handle_request_move;
	wl_signal_add(&xdg_surface->toplevel->events.request_move,
			&xdg_shell_view->request_move);

	xdg_shell_view->request_resize.notify = handle_request_resize;
	wl_signal_add(&xdg_surface->toplevel->events.request_resize,
			&xdg_shell_view->request_resize);

	xdg_shell_view->set_title.notify = handle_set_title;
	wl_signal_add(&xdg_surface->toplevel->events.set_title,
			&xdg_shell_view->set_title);

	xdg_shell_view->set_app_id.notify = handle_set_app_id;
	wl_signal_add(&xdg_surface->toplevel->events.set_app_id,
			&xdg_shell_view->set_app_id);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_xdg_shell_view *xdg_shell_view =
		wl_container_of(listener, xdg_shell_view, destroy);
	struct sway_view *view = &xdg_shell_view->view;
	if (!sway_assert(view->surface == NULL, "Tried to destroy a mapped view")) {
		return;
	}
	wl_list_remove(&xdg_shell_view->destroy.link);
	wl_list_remove(&xdg_shell_view->map.link);
	wl_list_remove(&xdg_shell_view->unmap.link);
	view->wlr_xdg_surface = NULL;
	if (view->xdg_decoration) {
		view->xdg_decoration->view = NULL;
	}
	view_begin_destroy(view);
}

struct sway_view *view_from_wlr_xdg_surface(
		struct wlr_xdg_surface *xdg_surface) {
	return xdg_surface->data;
}

void handle_xdg_shell_surface(struct wl_listener *listener, void *data) {
	struct wlr_xdg_surface *xdg_surface = data;

	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		sway_log(SWAY_DEBUG, "New xdg_shell popup");
		return;
	}

	sway_log(SWAY_DEBUG, "New xdg_shell toplevel title='%s' app_id='%s'",
		xdg_surface->toplevel->title, xdg_surface->toplevel->app_id);
	wlr_xdg_surface_ping(xdg_surface);

	struct sway_xdg_shell_view *xdg_shell_view =
		calloc(1, sizeof(struct sway_xdg_shell_view));
	if (!sway_assert(xdg_shell_view, "Failed to allocate view")) {
		return;
	}

	view_init(&xdg_shell_view->view, SWAY_VIEW_XDG_SHELL, &view_impl);
	xdg_shell_view->view.wlr_xdg_surface = xdg_surface;

	xdg_shell_view->map.notify = handle_map;
	wl_signal_add(&xdg_surface->events.map, &xdg_shell_view->map);

	xdg_shell_view->unmap.notify = handle_unmap;
	wl_signal_add(&xdg_surface->events.unmap, &xdg_shell_view->unmap);

	xdg_shell_view->destroy.notify = handle_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &xdg_shell_view->destroy);

	xdg_surface->data = xdg_shell_view;
}
