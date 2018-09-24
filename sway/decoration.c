#include <stdlib.h>
#include "sway/decoration.h"
#include "sway/server.h"
#include "sway/tree/view.h"
#include "log.h"

static void server_decoration_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_server_decoration *deco =
		wl_container_of(listener, deco, destroy);
	deco->view->decoration = NULL;
	wl_list_remove(&deco->destroy.link);
	wl_list_remove(&deco->mode.link);
	wl_list_remove(&deco->link);
	free(deco);
}

static void server_decoration_handle_mode(struct wl_listener *listener,
		void *data) {
	struct sway_server_decoration *deco =
		wl_container_of(listener, deco, mode);
	struct sway_view *view =
		view_from_wlr_surface(deco->wlr_server_decoration->surface);
	if (view == NULL || view->surface != deco->wlr_server_decoration->surface) {
		return;
	}

	switch (view->type) {
	case SWAY_VIEW_XDG_SHELL_V6:;
		struct sway_xdg_shell_v6_view *xdg_shell_v6_view =
			(struct sway_xdg_shell_v6_view *)view;
		xdg_shell_v6_view->deco_mode = deco->wlr_server_decoration->mode;
		break;
	case SWAY_VIEW_XDG_SHELL:;
		struct sway_xdg_shell_view *xdg_shell_view =
			(struct sway_xdg_shell_view *)view;
		xdg_shell_view->deco_mode = deco->wlr_server_decoration->mode;
		break;
	default:
		break;
	}
}

void handle_server_decoration(struct wl_listener *listener, void *data) {
	struct wlr_server_decoration *wlr_deco = data;

	struct sway_server_decoration *deco = calloc(1, sizeof(*deco));
	if (deco == NULL) {
		return;
	}

	deco->wlr_server_decoration = wlr_deco;
	deco->view = view_from_wlr_surface(wlr_deco->surface);
	deco->view->decoration = deco;

	wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
	deco->destroy.notify = server_decoration_handle_destroy;

	wl_signal_add(&wlr_deco->events.mode, &deco->mode);
	deco->mode.notify = server_decoration_handle_mode;

	wl_list_insert(&server.decorations, &deco->link);
}

struct sway_server_decoration *decoration_from_surface(
		struct wlr_surface *surface) {
	struct sway_server_decoration *deco;
	wl_list_for_each(deco, &server.decorations, link) {
		if (deco->wlr_server_decoration->surface == surface) {
			return deco;
		}
	}
	return NULL;
}
