#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output_layout.h>
#include "log.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/container.h"
#include "sway/tree/layout.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

void view_init(struct sway_view *view, enum sway_view_type type,
		const struct sway_view_impl *impl) {
	view->type = type;
	view->impl = impl;
	wl_signal_init(&view->events.unmap);
}

void view_destroy(struct sway_view *view) {
	if (view == NULL) {
		return;
	}

	if (view->surface != NULL) {
		view_unmap(view);
	}

	container_destroy(view->swayc);

	if (view->impl->destroy) {
		view->impl->destroy(view);
	} else {
		free(view);
	}
}

const char *view_get_title(struct sway_view *view) {
	if (view->impl->get_prop) {
		return view->impl->get_prop(view, VIEW_PROP_TITLE);
	}
	return NULL;
}

const char *view_get_app_id(struct sway_view *view) {
	if (view->impl->get_prop) {
		return view->impl->get_prop(view, VIEW_PROP_APP_ID);
	}
	return NULL;
}

const char *view_get_class(struct sway_view *view) {
	if (view->impl->get_prop) {
		return view->impl->get_prop(view, VIEW_PROP_CLASS);
	}
	return NULL;
}

const char *view_get_instance(struct sway_view *view) {
	if (view->impl->get_prop) {
		return view->impl->get_prop(view, VIEW_PROP_INSTANCE);
	}
	return NULL;
}

void view_configure(struct sway_view *view, double ox, double oy, int width,
		int height) {
	if (view->impl->configure) {
		view->impl->configure(view, ox, oy, width, height);
	}
}

void view_set_activated(struct sway_view *view, bool activated) {
	if (view->impl->set_activated) {
		view->impl->set_activated(view, activated);
	}
}

void view_set_fullscreen(struct sway_view *view, bool fullscreen) {
	if (view->is_fullscreen == fullscreen) {
		return;
	}

	struct sway_container *workspace = container_parent(view->swayc, C_WORKSPACE);
	struct sway_container *container = container_parent(workspace, C_OUTPUT);
	struct sway_output *output = container->sway_output;

	if (view->impl->set_fullscreen) {
		view->impl->set_fullscreen(view, fullscreen);
	}

	view->is_fullscreen = fullscreen;

	if (fullscreen) {
		if (workspace->sway_workspace->fullscreen) {
			view_set_fullscreen(workspace->sway_workspace->fullscreen, false);
		}
		workspace->sway_workspace->fullscreen = view;

		struct sway_seat *seat;
		struct sway_container *focus, *focus_ws;
		wl_list_for_each(seat, &input_manager->seats, link) {
			focus = seat_get_focus(seat);
			focus_ws = focus;
			if (focus_ws->type != C_WORKSPACE) {
				focus_ws = container_parent(focus_ws, C_WORKSPACE);
			}
			seat_set_focus(seat, view->swayc);
			if (focus_ws != workspace) {
				seat_set_focus(seat, focus);
			}
		}
	} else {
		workspace->sway_workspace->fullscreen = NULL;
	}

	arrange_windows(workspace, -1, -1);
	output_damage_whole(output);

	ipc_event_window(view->swayc, "fullscreen_mode");
}

void view_close(struct sway_view *view) {
	if (view->impl->close) {
		view->impl->close(view);
	}
}

void view_damage(struct sway_view *view, bool whole) {
	for (int i = 0; i < root_container.children->length; ++i) {
		struct sway_container *cont = root_container.children->items[i];
		if (cont->type == C_OUTPUT) {
			output_damage_view(cont->sway_output, view, whole);
		}
	}
}

static void view_get_layout_box(struct sway_view *view, struct wlr_box *box) {
	struct sway_container *output = container_parent(view->swayc, C_OUTPUT);

	box->x = output->x + view->swayc->x;
	box->y = output->y + view->swayc->y;
	box->width = view->width;
	box->height = view->height;
}

void view_for_each_surface(struct sway_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	if (view->impl->for_each_surface) {
		view->impl->for_each_surface(view, iterator, user_data);
	} else {
		wlr_surface_for_each_surface(view->surface, iterator, user_data);
	}
}

static void view_subsurface_create(struct sway_view *view,
	struct wlr_subsurface *subsurface);

static void view_init_subsurfaces(struct sway_view *view,
	struct wlr_surface *surface);

static void view_handle_surface_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct sway_view *view =
		wl_container_of(listener, view, surface_new_subsurface);
	struct wlr_subsurface *subsurface = data;
	view_subsurface_create(view, subsurface);
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_leave(surface, wlr_output);
}

static void view_handle_container_reparent(struct wl_listener *listener,
		void *data) {
	struct sway_view *view =
		wl_container_of(listener, view, container_reparent);
	struct sway_container *old_parent = data;

	struct sway_container *old_output = old_parent;
	if (old_output != NULL && old_output->type != C_OUTPUT) {
		old_output = container_parent(old_output, C_OUTPUT);
	}

	struct sway_container *new_output = view->swayc->parent;
	if (new_output != NULL && new_output->type != C_OUTPUT) {
		new_output = container_parent(new_output, C_OUTPUT);
	}

	if (old_output == new_output) {
		return;
	}

	if (old_output != NULL) {
		view_for_each_surface(view, surface_send_leave_iterator,
			old_output->sway_output->wlr_output);
	}
	if (new_output != NULL) {
		view_for_each_surface(view, surface_send_enter_iterator,
			new_output->sway_output->wlr_output);
	}
}

void view_map(struct sway_view *view, struct wlr_surface *wlr_surface) {
	if (!sway_assert(view->surface == NULL, "cannot map mapped view")) {
		return;
	}

	struct sway_seat *seat = input_manager_current_seat(input_manager);
	struct sway_container *focus = seat_get_focus_inactive(seat,
		&root_container);
	struct sway_container *cont = container_view_create(focus, view);

	view->surface = wlr_surface;
	view->swayc = cont;

	view_init_subsurfaces(view, wlr_surface);
	wl_signal_add(&wlr_surface->events.new_subsurface,
		&view->surface_new_subsurface);
	view->surface_new_subsurface.notify = view_handle_surface_new_subsurface;

	wl_signal_add(&view->swayc->events.reparent, &view->container_reparent);
	view->container_reparent.notify = view_handle_container_reparent;

	arrange_windows(cont->parent, -1, -1);
	input_manager_set_focus(input_manager, cont);

	view_damage(view, true);
	view_handle_container_reparent(&view->container_reparent, NULL);
}

void view_unmap(struct sway_view *view) {
	if (!sway_assert(view->surface != NULL, "cannot unmap unmapped view")) {
		return;
	}

	wl_signal_emit(&view->events.unmap, view);

	if (view->is_fullscreen) {
		struct sway_container *ws = container_parent(view->swayc, C_WORKSPACE);
		ws->sway_workspace->fullscreen = NULL;
	}

	view_damage(view, true);

	wl_list_remove(&view->surface_new_subsurface.link);
	wl_list_remove(&view->container_reparent.link);

	struct sway_container *parent = container_destroy(view->swayc);

	view->swayc = NULL;
	view->surface = NULL;

	arrange_windows(parent, -1, -1);
}

void view_update_position(struct sway_view *view, double ox, double oy) {
	if (view->swayc->x == ox && view->swayc->y == oy) {
		return;
	}

	view_damage(view, true);
	view->swayc->x = ox;
	view->swayc->y = oy;
	view_damage(view, true);
}

void view_update_size(struct sway_view *view, int width, int height) {
	if (view->width == width && view->height == height) {
		return;
	}

	view_damage(view, true);
	view->width = width;
	view->height = height;
	view_damage(view, true);
}


static void view_subsurface_create(struct sway_view *view,
		struct wlr_subsurface *subsurface) {
	struct sway_view_child *child = calloc(1, sizeof(struct sway_view_child));
	if (child == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return;
	}
	view_child_init(child, NULL, view, subsurface->surface);
}

static void view_child_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct sway_view_child *child =
		wl_container_of(listener, child, surface_commit);
	// TODO: only accumulate damage from the child
	view_damage(child->view, false);
}

static void view_child_handle_surface_new_subsurface(
		struct wl_listener *listener, void *data) {
	struct sway_view_child *child =
		wl_container_of(listener, child, surface_new_subsurface);
	struct wlr_subsurface *subsurface = data;
	view_subsurface_create(child->view, subsurface);
}

static void view_child_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_view_child *child =
		wl_container_of(listener, child, surface_destroy);
	view_child_destroy(child);
}

static void view_child_handle_view_unmap(struct wl_listener *listener,
		void *data) {
	struct sway_view_child *child =
		wl_container_of(listener, child, view_unmap);
	view_child_destroy(child);
}

static void view_init_subsurfaces(struct sway_view *view,
		struct wlr_surface *surface) {
	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		view_subsurface_create(view, subsurface);
	}
}

void view_child_init(struct sway_view_child *child,
		const struct sway_view_child_impl *impl, struct sway_view *view,
		struct wlr_surface *surface) {
	child->impl = impl;
	child->view = view;
	child->surface = surface;

	wl_signal_add(&surface->events.commit, &child->surface_commit);
	child->surface_commit.notify = view_child_handle_surface_commit;
	wl_signal_add(&surface->events.new_subsurface,
		&child->surface_new_subsurface);
	child->surface_new_subsurface.notify =
		view_child_handle_surface_new_subsurface;
	wl_signal_add(&surface->events.destroy, &child->surface_destroy);
	child->surface_destroy.notify = view_child_handle_surface_destroy;
	wl_signal_add(&view->events.unmap, &child->view_unmap);
	child->view_unmap.notify = view_child_handle_view_unmap;

	struct sway_container *output = child->view->swayc->parent;
	if (output != NULL) {
		if (output->type != C_OUTPUT) {
			output = container_parent(output, C_OUTPUT);
		}
		wlr_surface_send_enter(child->surface, output->sway_output->wlr_output);
	}

	view_init_subsurfaces(child->view, surface);

	// TODO: only damage the whole child
	view_damage(child->view, true);
}

void view_child_destroy(struct sway_view_child *child) {
	// TODO: only damage the whole child
	view_damage(child->view, true);

	wl_list_remove(&child->surface_commit.link);
	wl_list_remove(&child->surface_destroy.link);
	wl_list_remove(&child->view_unmap.link);

	if (child->impl && child->impl->destroy) {
		child->impl->destroy(child);
	} else {
		free(child);
	}
}
