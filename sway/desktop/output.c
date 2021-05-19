#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend/drm.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/region.h>
#include "config.h"
#include "log.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/layers.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/surface.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"

struct sway_output *output_by_name_or_id(const char *name_or_id) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		char identifier[128];
		output_get_identifier(identifier, sizeof(identifier), output);
		if (strcasecmp(identifier, name_or_id) == 0
				|| strcasecmp(output->wlr_output->name, name_or_id) == 0) {
			return output;
		}
	}
	return NULL;
}

struct sway_output *all_output_by_name_or_id(const char *name_or_id) {
	struct sway_output *output;
	wl_list_for_each(output, &root->all_outputs, link) {
		char identifier[128];
		output_get_identifier(identifier, sizeof(identifier), output);
		if (strcasecmp(identifier, name_or_id) == 0
				|| strcasecmp(output->wlr_output->name, name_or_id) == 0) {
			return output;
		}
	}
	return NULL;
}

struct surface_iterator_data {
	sway_surface_iterator_func_t user_iterator;
	void *user_data;

	struct sway_output *output;
	struct sway_view *view;
	double ox, oy;
	int width, height;
};

static bool get_surface_box(struct surface_iterator_data *data,
		struct wlr_surface *surface, int sx, int sy,
		struct wlr_box *surface_box) {
	struct sway_output *output = data->output;

	if (!wlr_surface_has_buffer(surface)) {
		return false;
	}

	int sw = surface->current.width;
	int sh = surface->current.height;

	struct wlr_box box = {
		.x = floor(data->ox + sx),
		.y = floor(data->oy + sy),
		.width = sw,
		.height = sh,
	};
	if (surface_box != NULL) {
		memcpy(surface_box, &box, sizeof(struct wlr_box));
	}

	struct wlr_box output_box = {
		.width = output->width,
		.height = output->height,
	};

	struct wlr_box intersection;
	return wlr_box_intersection(&intersection, &output_box, &box);
}

static void output_for_each_surface_iterator(struct wlr_surface *surface,
		int sx, int sy, void *_data) {
	struct surface_iterator_data *data = _data;

	struct wlr_box box;
	bool intersects = get_surface_box(data, surface, sx, sy, &box);
	if (!intersects) {
		return;
	}

	data->user_iterator(data->output, data->view, surface, &box,
		data->user_data);
}

void output_surface_for_each_surface(struct sway_output *output,
		struct wlr_surface *surface, double ox, double oy,
		sway_surface_iterator_func_t iterator, void *user_data) {
	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.view = NULL,
		.ox = ox,
		.oy = oy,
		.width = surface->current.width,
		.height = surface->current.height,
	};

	wlr_surface_for_each_surface(surface,
		output_for_each_surface_iterator, &data);
}

void output_view_for_each_surface(struct sway_output *output,
		struct sway_view *view, sway_surface_iterator_func_t iterator,
		void *user_data) {
	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.view = view,
		.ox = view->container->surface_x - output->lx
			- view->geometry.x,
		.oy = view->container->surface_y - output->ly
			- view->geometry.y,
		.width = view->container->current.content_width,
		.height = view->container->current.content_height,
	};

	view_for_each_surface(view, output_for_each_surface_iterator, &data);
}

void output_view_for_each_popup_surface(struct sway_output *output,
		struct sway_view *view, sway_surface_iterator_func_t iterator,
		void *user_data) {
	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.view = view,
		.ox = view->container->surface_x - output->lx
			- view->geometry.x,
		.oy = view->container->surface_y - output->ly
			- view->geometry.y,
		.width = view->container->current.content_width,
		.height = view->container->current.content_height,
	};

	view_for_each_popup_surface(view, output_for_each_surface_iterator, &data);
}

void output_layer_for_each_surface(struct sway_output *output,
		struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
		void *user_data) {
	struct sway_layer_surface *layer_surface;
	wl_list_for_each(layer_surface, layer_surfaces, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
			layer_surface->layer_surface;
		struct wlr_surface *surface = wlr_layer_surface_v1->surface;
		struct surface_iterator_data data = {
			.user_iterator = iterator,
			.user_data = user_data,
			.output = output,
			.view = NULL,
			.ox = layer_surface->geo.x,
			.oy = layer_surface->geo.y,
			.width = surface->current.width,
			.height = surface->current.height,
		};
		wlr_layer_surface_v1_for_each_surface(wlr_layer_surface_v1,
			output_for_each_surface_iterator, &data);
	}
}

void output_layer_for_each_toplevel_surface(struct sway_output *output,
		struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
		void *user_data) {
	struct sway_layer_surface *layer_surface;
	wl_list_for_each(layer_surface, layer_surfaces, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
			layer_surface->layer_surface;
		output_surface_for_each_surface(output, wlr_layer_surface_v1->surface,
			layer_surface->geo.x, layer_surface->geo.y, iterator,
			user_data);
	}
}


void output_layer_for_each_popup_surface(struct sway_output *output,
		struct wl_list *layer_surfaces, sway_surface_iterator_func_t iterator,
		void *user_data) {
	struct sway_layer_surface *layer_surface;
	wl_list_for_each(layer_surface, layer_surfaces, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
			layer_surface->layer_surface;
		struct wlr_surface *surface = wlr_layer_surface_v1->surface;
		struct surface_iterator_data data = {
			.user_iterator = iterator,
			.user_data = user_data,
			.output = output,
			.view = NULL,
			.ox = layer_surface->geo.x,
			.oy = layer_surface->geo.y,
			.width = surface->current.width,
			.height = surface->current.height,
		};
		wlr_layer_surface_v1_for_each_popup_surface(wlr_layer_surface_v1,
			output_for_each_surface_iterator, &data);
	}
}

#if HAVE_XWAYLAND
void output_unmanaged_for_each_surface(struct sway_output *output,
		struct wl_list *unmanaged, sway_surface_iterator_func_t iterator,
		void *user_data) {
	struct sway_xwayland_unmanaged *unmanaged_surface;
	wl_list_for_each(unmanaged_surface, unmanaged, link) {
		struct wlr_xwayland_surface *xsurface =
			unmanaged_surface->wlr_xwayland_surface;
		double ox = unmanaged_surface->lx - output->lx;
		double oy = unmanaged_surface->ly - output->ly;

		output_surface_for_each_surface(output, xsurface->surface, ox, oy,
			iterator, user_data);
	}
}
#endif

void output_drag_icons_for_each_surface(struct sway_output *output,
		struct wl_list *drag_icons, sway_surface_iterator_func_t iterator,
		void *user_data) {
	struct sway_drag_icon *drag_icon;
	wl_list_for_each(drag_icon, drag_icons, link) {
		double ox = drag_icon->x - output->lx;
		double oy = drag_icon->y - output->ly;

		if (drag_icon->wlr_drag_icon->mapped) {
			output_surface_for_each_surface(output,
				drag_icon->wlr_drag_icon->surface, ox, oy,
				iterator, user_data);
		}
	}
}

static void for_each_surface_container_iterator(struct sway_container *con,
		void *_data) {
	if (!con->view || !view_is_visible(con->view)) {
		return;
	}

	struct surface_iterator_data *data = _data;
	output_view_for_each_surface(data->output, con->view,
		data->user_iterator, data->user_data);
}

static void output_for_each_surface(struct sway_output *output,
		sway_surface_iterator_func_t iterator, void *user_data) {
	if (output_has_opaque_overlay_layer_surface(output)) {
		goto overlay;
	}

	struct surface_iterator_data data = {
		.user_iterator = iterator,
		.user_data = user_data,
		.output = output,
		.view = NULL,
	};

	struct sway_workspace *workspace = output_get_active_workspace(output);
	struct sway_container *fullscreen_con = root->fullscreen_global;
	if (!fullscreen_con) {
		if (!workspace) {
			return;
		}
		fullscreen_con = workspace->current.fullscreen;
	}
	if (fullscreen_con) {
		for_each_surface_container_iterator(fullscreen_con, &data);
		container_for_each_child(fullscreen_con,
			for_each_surface_container_iterator, &data);

		// TODO: Show transient containers for fullscreen global
		if (fullscreen_con == workspace->current.fullscreen) {
			for (int i = 0; i < workspace->current.floating->length; ++i) {
				struct sway_container *floater =
					workspace->current.floating->items[i];
				if (container_is_transient_for(floater, fullscreen_con)) {
					for_each_surface_container_iterator(floater, &data);
				}
			}
		}
#if HAVE_XWAYLAND
		output_unmanaged_for_each_surface(output, &root->xwayland_unmanaged,
			iterator, user_data);
#endif
	} else {
		output_layer_for_each_surface(output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			iterator, user_data);
		output_layer_for_each_surface(output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			iterator, user_data);

		workspace_for_each_container(workspace,
			for_each_surface_container_iterator, &data);

#if HAVE_XWAYLAND
		output_unmanaged_for_each_surface(output, &root->xwayland_unmanaged,
			iterator, user_data);
#endif
		output_layer_for_each_surface(output,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			iterator, user_data);
	}

overlay:
	output_layer_for_each_surface(output,
		&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
		iterator, user_data);
	output_drag_icons_for_each_surface(output, &root->drag_icons,
		iterator, user_data);
}

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

struct sway_workspace *output_get_active_workspace(struct sway_output *output) {
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_node *focus = seat_get_active_tiling_child(seat, &output->node);
	if (!focus) {
		if (!output->workspaces->length) {
			return NULL;
		}
		return output->workspaces->items[0];
	}
	return focus->sway_workspace;
}

bool output_has_opaque_overlay_layer_surface(struct sway_output *output) {
	struct sway_layer_surface *sway_layer_surface;
	wl_list_for_each(sway_layer_surface,
			&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], link) {
		struct wlr_surface *wlr_surface = sway_layer_surface->layer_surface->surface;
		pixman_box32_t output_box = {
			.x2 = output->width,
			.y2 = output->height,
		};
		pixman_region32_t surface_opaque_box;
		pixman_region32_init(&surface_opaque_box);
		pixman_region32_copy(&surface_opaque_box, &wlr_surface->opaque_region);
		pixman_region32_translate(&surface_opaque_box,
			sway_layer_surface->geo.x, sway_layer_surface->geo.y);
		pixman_region_overlap_t contains =
			pixman_region32_contains_rectangle(&surface_opaque_box, &output_box);
		pixman_region32_fini(&surface_opaque_box);

		if (contains == PIXMAN_REGION_IN) {
			return true;
		}
	}
	return false;
}

struct send_frame_done_data {
	struct timespec when;
	int msec_until_refresh;
};

static void send_frame_done_iterator(struct sway_output *output,
		struct sway_view *view, struct wlr_surface *surface,
		struct wlr_box *box, void *user_data) {
	int view_max_render_time = 0;
	if (view != NULL) {
		view_max_render_time = view->max_render_time;
	}

	struct send_frame_done_data *data = user_data;

	int delay = data->msec_until_refresh - output->max_render_time
			- view_max_render_time;

	if (output->max_render_time == 0 || view_max_render_time == 0 || delay < 1) {
		wlr_surface_send_frame_done(surface, &data->when);
	} else {
		struct sway_surface *sway_surface = surface->data;
		wl_event_source_timer_update(sway_surface->frame_done_timer, delay);
	}
}

static void send_frame_done(struct sway_output *output, struct send_frame_done_data *data) {
	output_for_each_surface(output, send_frame_done_iterator, data);
}

static void count_surface_iterator(struct sway_output *output,
		struct sway_view *view, struct wlr_surface *surface,
		struct wlr_box *box, void *data) {
	size_t *n = data;
	(*n)++;
}

static bool scan_out_fullscreen_view(struct sway_output *output,
		struct sway_view *view) {
	struct wlr_output *wlr_output = output->wlr_output;
	struct sway_workspace *workspace = output->current.active_workspace;
	if (!sway_assert(workspace, "Expected an active workspace")) {
		return false;
	}

	if (!wl_list_empty(&view->saved_buffers)) {
		return false;
	}

	for (int i = 0; i < workspace->current.floating->length; ++i) {
		struct sway_container *floater =
			workspace->current.floating->items[i];
		if (container_is_transient_for(floater, view->container)) {
			return false;
		}
	}

#if HAVE_XWAYLAND
	if (!wl_list_empty(&root->xwayland_unmanaged)) {
		return false;
	}
#endif

	if (!wl_list_empty(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY])) {
		return false;
	}
	if (!wl_list_empty(&root->drag_icons)) {
		return false;
	}

	struct wlr_surface *surface = view->surface;
	if (surface == NULL) {
		return false;
	}
	size_t n_surfaces = 0;
	output_view_for_each_surface(output, view,
		count_surface_iterator, &n_surfaces);
	if (n_surfaces != 1) {
		return false;
	}

	if (surface->buffer == NULL) {
		return false;
	}

	if ((float)surface->current.scale != wlr_output->scale ||
			surface->current.transform != wlr_output->transform) {
		return false;
	}

	wlr_output_attach_buffer(wlr_output, &surface->buffer->base);
	if (!wlr_output_test(wlr_output)) {
		return false;
	}

	wlr_presentation_surface_sampled_on_output(server.presentation, surface,
		wlr_output);

	return wlr_output_commit(wlr_output);
}

static int output_repaint_timer_handler(void *data) {
	struct sway_output *output = data;
	if (output->wlr_output == NULL) {
		return 0;
	}

	output->wlr_output->frame_pending = false;

	struct sway_workspace *workspace = output->current.active_workspace;
	if (workspace == NULL) {
		return 0;
	}

	struct sway_container *fullscreen_con = root->fullscreen_global;
	if (!fullscreen_con) {
		fullscreen_con = workspace->current.fullscreen;
	}

	if (fullscreen_con && fullscreen_con->view) {
		// Try to scan-out the fullscreen view
		static bool last_scanned_out = false;
		bool scanned_out =
			scan_out_fullscreen_view(output, fullscreen_con->view);

		if (scanned_out && !last_scanned_out) {
			sway_log(SWAY_DEBUG, "Scanning out fullscreen view on %s",
				output->wlr_output->name);
		}
		if (last_scanned_out && !scanned_out) {
			sway_log(SWAY_DEBUG, "Stopping fullscreen view scan out on %s",
				output->wlr_output->name);
			output_damage_whole(output);
		}
		last_scanned_out = scanned_out;

		if (scanned_out) {
			return 0;
		}
	}

	bool needs_frame;
	pixman_region32_t damage;
	pixman_region32_init(&damage);
	if (!wlr_output_damage_attach_render(output->damage,
			&needs_frame, &damage)) {
		return 0;
	}

	if (needs_frame) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);

		output_render(output, &now, &damage);
	} else {
		wlr_output_rollback(output->wlr_output);
	}

	pixman_region32_fini(&damage);

	return 0;
}

static void damage_handle_frame(struct wl_listener *listener, void *user_data) {
	struct sway_output *output =
		wl_container_of(listener, output, damage_frame);
	if (!output->enabled || !output->wlr_output->enabled) {
		return;
	}

	// Compute predicted milliseconds until the next refresh. It's used for
	// delaying both output rendering and surface frame callbacks.
	int msec_until_refresh = 0;

	if (output->max_render_time != 0) {
		struct timespec now;
		clockid_t presentation_clock
			= wlr_backend_get_presentation_clock(server.backend);
		clock_gettime(presentation_clock, &now);

		const long NSEC_IN_SECONDS = 1000000000;
		struct timespec predicted_refresh = output->last_presentation;
		predicted_refresh.tv_nsec += output->refresh_nsec % NSEC_IN_SECONDS;
		predicted_refresh.tv_sec += output->refresh_nsec / NSEC_IN_SECONDS;
		if (predicted_refresh.tv_nsec >= NSEC_IN_SECONDS) {
			predicted_refresh.tv_sec += 1;
			predicted_refresh.tv_nsec -= NSEC_IN_SECONDS;
		}

		// If the predicted refresh time is before the current time then
		// there's no point in delaying.
		//
		// We only check tv_sec because if the predicted refresh time is less
		// than a second before the current time, then msec_until_refresh will
		// end up slightly below zero, which will effectively disable the delay
		// without potential disastrous negative overflows that could occur if
		// tv_sec was not checked.
		if (predicted_refresh.tv_sec >= now.tv_sec) {
			long nsec_until_refresh
				= (predicted_refresh.tv_sec - now.tv_sec) * NSEC_IN_SECONDS
					+ (predicted_refresh.tv_nsec - now.tv_nsec);

			// We want msec_until_refresh to be conservative, that is, floored.
			// If we have 7.9 msec until refresh, we better compute the delay
			// as if we had only 7 msec, so that we don't accidentally delay
			// more than necessary and miss a frame.
			msec_until_refresh = nsec_until_refresh / 1000000;
		}
	}

	int delay = msec_until_refresh - output->max_render_time;

	// If the delay is less than 1 millisecond (which is the least we can wait)
	// then just render right away.
	if (delay < 1) {
		output_repaint_timer_handler(output);
	} else {
		output->wlr_output->frame_pending = true;
		wl_event_source_timer_update(output->repaint_timer, delay);
	}

	// Send frame done to all visible surfaces
	struct send_frame_done_data data = {0};
	clock_gettime(CLOCK_MONOTONIC, &data.when);
	data.msec_until_refresh = msec_until_refresh;
	send_frame_done(output, &data);
}

void output_damage_whole(struct sway_output *output) {
	// The output can exist with no wlr_output if it's just been disconnected
	// and the transaction to evacuate it has't completed yet.
	if (output && output->wlr_output && output->damage) {
		wlr_output_damage_add_whole(output->damage);
	}
}

static void damage_surface_iterator(struct sway_output *output,
		struct sway_view *view, struct wlr_surface *surface,
		struct wlr_box *_box, void *_data) {
	bool *data = _data;
	bool whole = *data;

	struct wlr_box box = *_box;
	scale_box(&box, output->wlr_output->scale);

	if (pixman_region32_not_empty(&surface->buffer_damage)) {
		pixman_region32_t damage;
		pixman_region32_init(&damage);
		wlr_surface_get_effective_damage(surface, &damage);
		wlr_region_scale(&damage, &damage, output->wlr_output->scale);
		if (ceil(output->wlr_output->scale) > surface->current.scale) {
			// When scaling up a surface, it'll become blurry so we need to
			// expand the damage region
			wlr_region_expand(&damage, &damage,
				ceil(output->wlr_output->scale) - surface->current.scale);
		}
		pixman_region32_translate(&damage, box.x, box.y);
		wlr_output_damage_add(output->damage, &damage);
		pixman_region32_fini(&damage);
	}

	if (whole) {
		wlr_output_damage_add_box(output->damage, &box);
	}

	if (!wl_list_empty(&surface->current.frame_callback_list)) {
		wlr_output_schedule_frame(output->wlr_output);
	}
}

void output_damage_surface(struct sway_output *output, double ox, double oy,
		struct wlr_surface *surface, bool whole) {
	output_surface_for_each_surface(output, surface, ox, oy,
		damage_surface_iterator, &whole);
}

void output_damage_from_view(struct sway_output *output,
		struct sway_view *view) {
	if (!view_is_visible(view)) {
		return;
	}
	bool whole = false;
	output_view_for_each_surface(output, view, damage_surface_iterator, &whole);
}

// Expecting an unscaled box in layout coordinates
void output_damage_box(struct sway_output *output, struct wlr_box *_box) {
	struct wlr_box box;
	memcpy(&box, _box, sizeof(struct wlr_box));
	box.x -= output->lx;
	box.y -= output->ly;
	scale_box(&box, output->wlr_output->scale);
	wlr_output_damage_add_box(output->damage, &box);
}

static void damage_child_views_iterator(struct sway_container *con,
		void *data) {
	if (!con->view || !view_is_visible(con->view)) {
		return;
	}
	struct sway_output *output = data;
	bool whole = true;
	output_view_for_each_surface(output, con->view, damage_surface_iterator,
			&whole);
}

void output_damage_whole_container(struct sway_output *output,
		struct sway_container *con) {
	// Pad the box by 1px, because the width is a double and might be a fraction
	struct wlr_box box = {
		.x = con->current.x - output->lx - 1,
		.y = con->current.y - output->ly - 1,
		.width = con->current.width + 2,
		.height = con->current.height + 2,
	};
	scale_box(&box, output->wlr_output->scale);
	wlr_output_damage_add_box(output->damage, &box);
	// Damage subsurfaces as well, which may extend outside the box
	if (con->view) {
		damage_child_views_iterator(con, output);
	} else {
		container_for_each_child(con, damage_child_views_iterator, output);
	}
}

static void damage_handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_output *output =
		wl_container_of(listener, output, damage_destroy);
	if (!output->enabled) {
		return;
	}
	output_disable(output);

	wl_list_remove(&output->damage_destroy.link);
	wl_list_remove(&output->damage_frame.link);

	transaction_commit_dirty();
}

static void update_output_manager_config(struct sway_server *server) {
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();

	struct sway_output *output;
	wl_list_for_each(output, &root->all_outputs, link) {
		if (output == root->noop_output) {
			continue;
		}
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(config, output->wlr_output);
		struct wlr_box *output_box = wlr_output_layout_get_box(
			root->output_layout, output->wlr_output);
		// We mark the output enabled even if it is switched off by DPMS
		config_head->state.enabled = output->current_mode != NULL && output->enabled;
		config_head->state.mode = output->current_mode;
		if (output_box) {
			config_head->state.x = output_box->x;
			config_head->state.y = output_box->y;
		}
	}

	wlr_output_manager_v1_set_configuration(server->output_manager_v1, config);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_output *output = wl_container_of(listener, output, destroy);
	struct sway_server *server = output->server;
	wl_signal_emit(&output->events.destroy, output);

	if (output->enabled) {
		output_disable(output);
	}
	output_begin_destroy(output);

	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->commit.link);
	wl_list_remove(&output->mode.link);
	wl_list_remove(&output->present.link);

	transaction_commit_dirty();

	update_output_manager_config(server);
}

static void handle_mode(struct wl_listener *listener, void *data) {
	struct sway_output *output = wl_container_of(listener, output, mode);
	if (!output->enabled && !output->enabling) {
		struct output_config *oc = find_output_config(output);
		if (output->wlr_output->current_mode != NULL &&
				(!oc || oc->enabled)) {
			// We want to enable this output, but it didn't work last time,
			// possibly because we hadn't enough CRTCs. Try again now that the
			// output has a mode.
			sway_log(SWAY_DEBUG, "Output %s has gained a CRTC, "
				"trying to enable it", output->wlr_output->name);
			apply_output_config(oc, output);
		}
		return;
	}
	if (!output->enabled) {
		return;
	}
	arrange_layers(output);
	arrange_output(output);
	transaction_commit_dirty();

	update_output_manager_config(output->server);
}

static void update_textures(struct sway_container *con, void *data) {
	container_update_title_textures(con);
	container_update_marks_textures(con);
}

static void handle_commit(struct wl_listener *listener, void *data) {
	struct sway_output *output = wl_container_of(listener, output, commit);
	struct wlr_output_event_commit *event = data;

	if (!output->enabled) {
		return;
	}

	if (event->committed & WLR_OUTPUT_STATE_SCALE) {
		output_for_each_container(output, update_textures, NULL);
	}

	if (event->committed & (WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_SCALE)) {
		arrange_layers(output);
		arrange_output(output);
		transaction_commit_dirty();

		update_output_manager_config(output->server);
	}
}

static void handle_present(struct wl_listener *listener, void *data) {
	struct sway_output *output = wl_container_of(listener, output, present);
	struct wlr_output_event_present *output_event = data;

	if (!output->enabled) {
		return;
	}

	output->last_presentation = *output_event->when;
	output->refresh_nsec = output_event->refresh;
}

void handle_new_output(struct wl_listener *listener, void *data) {
	struct sway_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;
	sway_log(SWAY_DEBUG, "New output %p: %s (non-desktop: %d)",
			wlr_output, wlr_output->name, wlr_output->non_desktop);

	if (wlr_output->non_desktop) {
		sway_log(SWAY_DEBUG, "Not configuring non-desktop output");
		if (server->drm_lease_manager) {
			wlr_drm_lease_v1_manager_offer_output(server->drm_lease_manager,
					wlr_output);
		}
		return;
	}

	struct sway_output *output = output_create(wlr_output);
	if (!output) {
		return;
	}
	output->server = server;
	output->damage = wlr_output_damage_create(wlr_output);

	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	output->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_output->events.commit, &output->commit);
	output->commit.notify = handle_commit;
	wl_signal_add(&wlr_output->events.mode, &output->mode);
	output->mode.notify = handle_mode;
	wl_signal_add(&wlr_output->events.present, &output->present);
	output->present.notify = handle_present;
	wl_signal_add(&output->damage->events.frame, &output->damage_frame);
	output->damage_frame.notify = damage_handle_frame;
	wl_signal_add(&output->damage->events.destroy, &output->damage_destroy);
	output->damage_destroy.notify = damage_handle_destroy;

	output->repaint_timer = wl_event_loop_add_timer(server->wl_event_loop,
		output_repaint_timer_handler, output);

	struct output_config *oc = find_output_config(output);
	apply_output_config(oc, output);
	free_output_config(oc);

	transaction_commit_dirty();

	update_output_manager_config(server);
}

void handle_output_layout_change(struct wl_listener *listener,
		void *data) {
	struct sway_server *server =
		wl_container_of(listener, server, output_layout_change);
	update_output_manager_config(server);
}

static void output_manager_apply(struct sway_server *server,
		struct wlr_output_configuration_v1 *config, bool test_only) {
	// TODO: perform atomic tests on the whole backend atomically

	struct wlr_output_configuration_head_v1 *config_head;
	// First disable outputs we need to disable
	bool ok = true;
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		struct sway_output *output = wlr_output->data;
		if (!output->enabled || config_head->state.enabled) {
			continue;
		}
		struct output_config *oc = new_output_config(output->wlr_output->name);
		oc->enabled = false;

		if (test_only) {
			ok &= test_output_config(oc, output);
		} else {
			oc = store_output_config(oc);
			ok &= apply_output_config(oc, output);
		}
	}

	// Then enable outputs that need to
	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		struct sway_output *output = wlr_output->data;
		if (!config_head->state.enabled) {
			continue;
		}
		struct output_config *oc = new_output_config(output->wlr_output->name);
		oc->enabled = true;
		if (config_head->state.mode != NULL) {
			struct wlr_output_mode *mode = config_head->state.mode;
			oc->width = mode->width;
			oc->height = mode->height;
			oc->refresh_rate = mode->refresh / 1000.f;
		} else {
			oc->width = config_head->state.custom_mode.width;
			oc->height = config_head->state.custom_mode.height;
			oc->refresh_rate =
				config_head->state.custom_mode.refresh / 1000.f;
		}
		oc->x = config_head->state.x;
		oc->y = config_head->state.y;
		oc->transform = config_head->state.transform;
		oc->scale = config_head->state.scale;

		if (test_only) {
			ok &= test_output_config(oc, output);
		} else {
			oc = store_output_config(oc);
			ok &= apply_output_config(oc, output);
		}
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);

	if (!test_only) {
		update_output_manager_config(server);
	}
}

void handle_output_manager_apply(struct wl_listener *listener, void *data) {
	struct sway_server *server =
		wl_container_of(listener, server, output_manager_apply);
	struct wlr_output_configuration_v1 *config = data;

	output_manager_apply(server, config, false);
}

void handle_output_manager_test(struct wl_listener *listener, void *data) {
	struct sway_server *server =
		wl_container_of(listener, server, output_manager_test);
	struct wlr_output_configuration_v1 *config = data;

	output_manager_apply(server, config, true);
}

void handle_output_power_manager_set_mode(struct wl_listener *listener,
		void *data) {
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct sway_output *output = event->output->data;

	struct output_config *oc = new_output_config(output->wlr_output->name);
	switch (event->mode) {
	case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
		oc->dpms_state = DPMS_OFF;
		break;
	case ZWLR_OUTPUT_POWER_V1_MODE_ON:
		oc->dpms_state = DPMS_ON;
		break;
	}
	oc = store_output_config(oc);
	apply_output_config(oc, output);
}
