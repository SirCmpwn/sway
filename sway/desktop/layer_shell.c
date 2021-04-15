#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_damage.h>
#include <wlr/types/wlr_output.h>
#include "log.h"
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/layers.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/workspace.h"

static void apply_exclusive(struct wlr_box *usable_area,
		uint32_t anchor, int32_t exclusive,
		int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left) {
	if (exclusive <= 0) {
		return;
	}
	struct {
		uint32_t singular_anchor;
		uint32_t anchor_triplet;
		int *positive_axis;
		int *negative_axis;
		int margin;
	} edges[] = {
		// Top
		{
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.anchor_triplet =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.positive_axis = &usable_area->y,
			.negative_axis = &usable_area->height,
			.margin = margin_top,
		},
		// Bottom
		{
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.anchor_triplet =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->height,
			.margin = margin_bottom,
		},
		// Left
		{
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
			.anchor_triplet =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = &usable_area->x,
			.negative_axis = &usable_area->width,
			.margin = margin_left,
		},
		// Right
		{
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
			.anchor_triplet =
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->width,
			.margin = margin_right,
		},
	};
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
		if ((anchor  == edges[i].singular_anchor || anchor == edges[i].anchor_triplet)
				&& exclusive + edges[i].margin > 0) {
			if (edges[i].positive_axis) {
				*edges[i].positive_axis += exclusive + edges[i].margin;
			}
			if (edges[i].negative_axis) {
				*edges[i].negative_axis -= exclusive + edges[i].margin;
			}
			break;
		}
	}
}

static void arrange_layer(struct sway_output *output, struct wl_list *list,
		struct wlr_box *usable_area, bool exclusive) {
	struct sway_layer_surface *sway_layer;
	struct wlr_box full_area = { 0 };
	wlr_output_effective_resolution(output->wlr_output,
			&full_area.width, &full_area.height);
	wl_list_for_each(sway_layer, list, link) {
		struct wlr_layer_surface_v1 *layer = sway_layer->layer_surface;
		struct wlr_layer_surface_v1_state *state = &layer->current;
		if (exclusive != (state->exclusive_zone > 0)) {
			continue;
		}
		struct wlr_box bounds;
		if (state->exclusive_zone == -1) {
			bounds = full_area;
		} else {
			bounds = *usable_area;
		}
		struct wlr_box box = {
			.width = state->desired_width,
			.height = state->desired_height
		};
		// Horizontal axis
		const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		if (box.width == 0) {
			box.x = bounds.x;
		} else if ((state->anchor & both_horiz) == both_horiz) {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x = bounds.x;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x = bounds.x + (bounds.width - box.width);
		} else {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		}
		// Vertical axis
		const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		if (box.height == 0) {
			box.y = bounds.y;
		} else if ((state->anchor & both_vert) == both_vert) {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y = bounds.y;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y = bounds.y + (bounds.height - box.height);
		} else {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		}
		// Margin
		if (box.width == 0) {
			box.x += state->margin.left;
			box.width = bounds.width -
				(state->margin.left + state->margin.right);
		} else if ((state->anchor & both_horiz) == both_horiz) {
			// don't apply margins
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x += state->margin.left;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x -= state->margin.right;
		}
		if (box.height == 0) {
			box.y += state->margin.top;
			box.height = bounds.height -
				(state->margin.top + state->margin.bottom);
		} else if ((state->anchor & both_vert) == both_vert) {
			// don't apply margins
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y += state->margin.top;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y -= state->margin.bottom;
		}
		if (box.width < 0 || box.height < 0) {
			// TODO: Bubble up a protocol error?
			wlr_layer_surface_v1_close(layer);
			continue;
		}
		// Apply
		sway_layer->geo = box;
		apply_exclusive(usable_area, state->anchor, state->exclusive_zone,
				state->margin.top, state->margin.right,
				state->margin.bottom, state->margin.left);
		wlr_layer_surface_v1_configure(layer, box.width, box.height);
	}
}

void arrange_layers(struct sway_output *output) {
	struct wlr_box usable_area = { 0 };
	wlr_output_effective_resolution(output->wlr_output,
			&usable_area.width, &usable_area.height);

	// Arrange exclusive surfaces from top->bottom
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, true);
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, true);
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, true);
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, true);

	if (memcmp(&usable_area, &output->usable_area,
				sizeof(struct wlr_box)) != 0) {
		sway_log(SWAY_DEBUG, "Usable area changed, rearranging output");
		memcpy(&output->usable_area, &usable_area, sizeof(struct wlr_box));
		arrange_output(output);
	}

	// Arrange non-exclusive surfaces from top->bottom
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
			&usable_area, false);
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
			&usable_area, false);
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
			&usable_area, false);
	arrange_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
			&usable_area, false);

	// Find topmost keyboard interactive layer, if such a layer exists
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	size_t nlayers = sizeof(layers_above_shell) / sizeof(layers_above_shell[0]);
	struct sway_layer_surface *layer, *topmost = NULL;
	for (size_t i = 0; i < nlayers; ++i) {
		wl_list_for_each_reverse(layer,
				&output->layers[layers_above_shell[i]], link) {
			if (layer->layer_surface->current.keyboard_interactive &&
					layer->layer_surface->mapped) {
				topmost = layer;
				break;
			}
		}
		if (topmost != NULL) {
			break;
		}
	}

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		if (topmost != NULL) {
			seat_set_focus_layer(seat, topmost->layer_surface);
		} else if (seat->focused_layer &&
				!seat->focused_layer->current.keyboard_interactive) {
			seat_set_focus_layer(seat, NULL);
		}
	}
}

static struct sway_layer_surface *find_mapped_layer_by_client(
		struct wl_client *client, struct wlr_output *ignore_output) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if (output->wlr_output == ignore_output) {
			continue;
		}
		// For now we'll only check the overlay layer
		struct sway_layer_surface *lsurface;
		wl_list_for_each(lsurface,
				&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], link) {
			struct wl_resource *resource = lsurface->layer_surface->resource;
			if (wl_resource_get_client(resource) == client
					&& lsurface->layer_surface->mapped) {
				return lsurface;
			}
		}
	}
	return NULL;
}

static void handle_output_destroy(struct wl_listener *listener, void *data) {
	struct sway_layer_surface *sway_layer =
		wl_container_of(listener, sway_layer, output_destroy);
	// Determine if this layer is being used by an exclusive client. If it is,
	// try and find another layer owned by this client to pass focus to.
	struct sway_seat *seat = input_manager_get_default_seat();
	struct wl_client *client =
		wl_resource_get_client(sway_layer->layer_surface->resource);
	bool set_focus = seat->exclusive_client == client;

	wl_list_remove(&sway_layer->output_destroy.link);
	wl_list_remove(&sway_layer->link);
	wl_list_init(&sway_layer->link);

	if (set_focus) {
		struct sway_layer_surface *layer =
			find_mapped_layer_by_client(client, sway_layer->layer_surface->output);
		if (layer) {
			seat_set_focus_layer(seat, layer->layer_surface);
		}
	}

	sway_layer->layer_surface->output = NULL;
	wlr_layer_surface_v1_close(sway_layer->layer_surface);
}

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct sway_layer_surface *layer =
		wl_container_of(listener, layer, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = layer->layer_surface;
	struct wlr_output *wlr_output = layer_surface->output;
	if (wlr_output == NULL) {
		return;
	}

	struct sway_output *output = wlr_output->data;
	struct wlr_box old_geo = layer->geo;
	arrange_layers(output);

	bool geo_changed =
		memcmp(&old_geo, &layer->geo, sizeof(struct wlr_box)) != 0;
	bool layer_changed = layer->layer != layer_surface->current.layer;
	if (layer_changed) {
		wl_list_remove(&layer->link);
		wl_list_insert(&output->layers[layer_surface->current.layer],
			&layer->link);
		layer->layer = layer_surface->current.layer;
	}
	if (geo_changed || layer_changed) {
		output_damage_surface(output, old_geo.x, old_geo.y,
			layer_surface->surface, true);
		output_damage_surface(output, layer->geo.x, layer->geo.y,
			layer_surface->surface, true);
	} else {
		output_damage_surface(output, layer->geo.x, layer->geo.y,
			layer_surface->surface, false);
	}

	transaction_commit_dirty();
}

static void unmap(struct sway_layer_surface *sway_layer) {
	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		if (seat->focused_layer == sway_layer->layer_surface) {
			seat_set_focus_layer(seat, NULL);
		}
	}

	cursor_rebase_all();

	struct wlr_output *wlr_output = sway_layer->layer_surface->output;
	if (wlr_output == NULL) {
		return;
	}
	struct sway_output *output = wlr_output->data;
	if (output == NULL) {
		return;
	}
	output_damage_surface(output, sway_layer->geo.x, sway_layer->geo.y,
		sway_layer->layer_surface->surface, true);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_layer_surface *sway_layer =
		wl_container_of(listener, sway_layer, destroy);
	sway_log(SWAY_DEBUG, "Layer surface destroyed (%s)",
		sway_layer->layer_surface->namespace);
	if (sway_layer->layer_surface->mapped) {
		unmap(sway_layer);
	}
	wl_list_remove(&sway_layer->link);
	wl_list_remove(&sway_layer->destroy.link);
	wl_list_remove(&sway_layer->map.link);
	wl_list_remove(&sway_layer->unmap.link);
	wl_list_remove(&sway_layer->surface_commit.link);
	wl_list_remove(&sway_layer->new_popup.link);
	wl_list_remove(&sway_layer->new_subsurface.link);
	if (sway_layer->layer_surface->output != NULL) {
		struct sway_output *output = sway_layer->layer_surface->output->data;
		if (output != NULL) {
			arrange_layers(output);
			transaction_commit_dirty();
		}
		wl_list_remove(&sway_layer->output_destroy.link);
		sway_layer->layer_surface->output = NULL;
	}
	free(sway_layer);
}

static void handle_map(struct wl_listener *listener, void *data) {
	struct sway_layer_surface *sway_layer = wl_container_of(listener,
			sway_layer, map);
	struct sway_output *output = sway_layer->layer_surface->output->data;
	output_damage_surface(output, sway_layer->geo.x, sway_layer->geo.y,
		sway_layer->layer_surface->surface, true);
	wlr_surface_send_enter(sway_layer->layer_surface->surface,
		sway_layer->layer_surface->output);
	cursor_rebase_all();
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct sway_layer_surface *sway_layer = wl_container_of(
			listener, sway_layer, unmap);
	unmap(sway_layer);
}

static void subsurface_damage(struct sway_layer_subsurface *subsurface,
		bool whole) {
	struct sway_layer_surface *layer = subsurface->layer_surface;
	struct wlr_output *wlr_output = layer->layer_surface->output;
	if (!wlr_output) {
		return;
	}
	struct sway_output *output = wlr_output->data;
	int ox = subsurface->wlr_subsurface->current.x + layer->geo.x;
	int oy = subsurface->wlr_subsurface->current.y + layer->geo.y;
	output_damage_surface(
			output, ox, oy, subsurface->wlr_subsurface->surface, whole);
}

static void subsurface_handle_unmap(struct wl_listener *listener, void *data) {
	struct sway_layer_subsurface *subsurface =
			wl_container_of(listener, subsurface, unmap);
	subsurface_damage(subsurface, true);
}

static void subsurface_handle_map(struct wl_listener *listener, void *data) {
	struct sway_layer_subsurface *subsurface =
			wl_container_of(listener, subsurface, map);
	subsurface_damage(subsurface, true);
}

static void subsurface_handle_commit(struct wl_listener *listener, void *data) {
	struct sway_layer_subsurface *subsurface =
			wl_container_of(listener, subsurface, commit);
	subsurface_damage(subsurface, false);
}

static void subsurface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_layer_subsurface *subsurface =
			wl_container_of(listener, subsurface, destroy);

	wl_list_remove(&subsurface->map.link);
	wl_list_remove(&subsurface->unmap.link);
	wl_list_remove(&subsurface->destroy.link);
	wl_list_remove(&subsurface->commit.link);
	free(subsurface);
}

static struct sway_layer_subsurface *create_subsurface(
		struct wlr_subsurface *wlr_subsurface,
		struct sway_layer_surface *layer_surface) {
	struct sway_layer_subsurface *subsurface =
			calloc(1, sizeof(struct sway_layer_subsurface));
	if (subsurface == NULL) {
		return NULL;
	}

	subsurface->wlr_subsurface = wlr_subsurface;
	subsurface->layer_surface = layer_surface;

	subsurface->map.notify = subsurface_handle_map;
	wl_signal_add(&wlr_subsurface->events.map, &subsurface->map);
	subsurface->unmap.notify = subsurface_handle_unmap;
	wl_signal_add(&wlr_subsurface->events.unmap, &subsurface->unmap);
	subsurface->destroy.notify = subsurface_handle_destroy;
	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->commit.notify = subsurface_handle_commit;
	wl_signal_add(&wlr_subsurface->surface->events.commit, &subsurface->commit);

	return subsurface;
}

static void handle_new_subsurface(struct wl_listener *listener, void *data) {
	struct sway_layer_surface *sway_layer_surface =
			wl_container_of(listener, sway_layer_surface, new_subsurface);
	struct wlr_subsurface *wlr_subsurface = data;
	create_subsurface(wlr_subsurface, sway_layer_surface);
}


static struct sway_layer_surface *popup_get_layer(
		struct sway_layer_popup *popup) {
	while (popup->parent_type == LAYER_PARENT_POPUP) {
		popup = popup->parent_popup;
	}
	return popup->parent_layer;
}

static void popup_damage(struct sway_layer_popup *layer_popup, bool whole) {
	struct wlr_xdg_popup *popup = layer_popup->wlr_popup;
	struct wlr_surface *surface = popup->base->surface;
	int popup_sx = popup->geometry.x - popup->base->geometry.x;
	int popup_sy = popup->geometry.y - popup->base->geometry.y;
	int ox = popup_sx, oy = popup_sy;
	struct sway_layer_surface *layer;
	while (true) {
		if (layer_popup->parent_type == LAYER_PARENT_POPUP) {
			layer_popup = layer_popup->parent_popup;
			ox += layer_popup->wlr_popup->geometry.x;
			oy += layer_popup->wlr_popup->geometry.y;
		} else {
			layer = layer_popup->parent_layer;
			ox += layer->geo.x;
			oy += layer->geo.y;
			break;
		}
	}
	struct wlr_output *wlr_output = layer->layer_surface->output;
	struct sway_output *output = wlr_output->data;
	output_damage_surface(output, ox, oy, surface, whole);
}

static void popup_handle_map(struct wl_listener *listener, void *data) {
	struct sway_layer_popup *popup = wl_container_of(listener, popup, map);
	struct sway_layer_surface *layer = popup_get_layer(popup);
	struct wlr_output *wlr_output = layer->layer_surface->output;
	wlr_surface_send_enter(popup->wlr_popup->base->surface, wlr_output);
	popup_damage(popup, true);
}

static void popup_handle_unmap(struct wl_listener *listener, void *data) {
	struct sway_layer_popup *popup = wl_container_of(listener, popup, unmap);
	popup_damage(popup, true);
}

static void popup_handle_commit(struct wl_listener *listener, void *data) {
	struct sway_layer_popup *popup = wl_container_of(listener, popup, commit);
	popup_damage(popup, false);
}

static void popup_handle_destroy(struct wl_listener *listener, void *data) {
	struct sway_layer_popup *popup =
		wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->map.link);
	wl_list_remove(&popup->unmap.link);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->commit.link);
	free(popup);
}

static void popup_unconstrain(struct sway_layer_popup *popup) {
	struct sway_layer_surface *layer = popup_get_layer(popup);
	struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;

	struct sway_output *output = layer->layer_surface->output->data;

	// the output box expressed in the coordinate system of the toplevel parent
	// of the popup
	struct wlr_box output_toplevel_sx_box = {
		.x = -layer->geo.x,
		.y = -layer->geo.y,
		.width = output->width,
		.height = output->height,
	};

	wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static void popup_handle_new_popup(struct wl_listener *listener, void *data);

static struct sway_layer_popup *create_popup(struct wlr_xdg_popup *wlr_popup,
		enum layer_parent parent_type, void *parent) {
	struct sway_layer_popup *popup =
		calloc(1, sizeof(struct sway_layer_popup));
	if (popup == NULL) {
		return NULL;
	}

	popup->wlr_popup = wlr_popup;
	popup->parent_type = parent_type;
	popup->parent_layer = parent;

	popup->map.notify = popup_handle_map;
	wl_signal_add(&wlr_popup->base->events.map, &popup->map);
	popup->unmap.notify = popup_handle_unmap;
	wl_signal_add(&wlr_popup->base->events.unmap, &popup->unmap);
	popup->destroy.notify = popup_handle_destroy;
	wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);
	popup->commit.notify = popup_handle_commit;
	wl_signal_add(&wlr_popup->base->surface->events.commit, &popup->commit);
	popup->new_popup.notify = popup_handle_new_popup;
	wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

	popup_unconstrain(popup);

	return popup;
}

static void popup_handle_new_popup(struct wl_listener *listener, void *data) {
	struct sway_layer_popup *sway_layer_popup =
		wl_container_of(listener, sway_layer_popup, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	create_popup(wlr_popup, LAYER_PARENT_POPUP, sway_layer_popup);
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct sway_layer_surface *sway_layer_surface =
		wl_container_of(listener, sway_layer_surface, new_popup);
	struct wlr_xdg_popup *wlr_popup = data;
	create_popup(wlr_popup, LAYER_PARENT_LAYER, sway_layer_surface);
}

struct sway_layer_surface *layer_from_wlr_layer_surface_v1(
		struct wlr_layer_surface_v1 *layer_surface) {
	return layer_surface->data;
}

void handle_layer_shell_surface(struct wl_listener *listener, void *data) {
	struct wlr_layer_surface_v1 *layer_surface = data;
	sway_log(SWAY_DEBUG, "new layer surface: namespace %s layer %d anchor %" PRIu32
			" size %" PRIu32 "x%" PRIu32 " margin %" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",",
		layer_surface->namespace,
		layer_surface->client_pending.layer,
		layer_surface->client_pending.anchor,
		layer_surface->client_pending.desired_width,
		layer_surface->client_pending.desired_height,
		layer_surface->client_pending.margin.top,
		layer_surface->client_pending.margin.right,
		layer_surface->client_pending.margin.bottom,
		layer_surface->client_pending.margin.left);

	if (!layer_surface->output) {
		// Assign last active output
		struct sway_output *output = NULL;
		struct sway_seat *seat = input_manager_get_default_seat();
		if (seat) {
			struct sway_workspace *ws = seat_get_focused_workspace(seat);
			if (ws != NULL) {
				output = ws->output;
			}
		}
		if (!output || output == root->noop_output) {
			if (!root->outputs->length) {
				sway_log(SWAY_ERROR,
						"no output to auto-assign layer surface '%s' to",
						layer_surface->namespace);
				wlr_layer_surface_v1_close(layer_surface);
				return;
			}
			output = root->outputs->items[0];
		}
		layer_surface->output = output->wlr_output;
	}

	struct sway_layer_surface *sway_layer =
		calloc(1, sizeof(struct sway_layer_surface));
	if (!sway_layer) {
		return;
	}

	sway_layer->surface_commit.notify = handle_surface_commit;
	wl_signal_add(&layer_surface->surface->events.commit,
		&sway_layer->surface_commit);

	sway_layer->destroy.notify = handle_destroy;
	wl_signal_add(&layer_surface->events.destroy, &sway_layer->destroy);
	sway_layer->map.notify = handle_map;
	wl_signal_add(&layer_surface->events.map, &sway_layer->map);
	sway_layer->unmap.notify = handle_unmap;
	wl_signal_add(&layer_surface->events.unmap, &sway_layer->unmap);
	sway_layer->new_popup.notify = handle_new_popup;
	wl_signal_add(&layer_surface->events.new_popup, &sway_layer->new_popup);
	sway_layer->new_subsurface.notify = handle_new_subsurface;
	wl_signal_add(&layer_surface->surface->events.new_subsurface,
			&sway_layer->new_subsurface);

	sway_layer->layer_surface = layer_surface;
	layer_surface->data = sway_layer;

	struct sway_output *output = layer_surface->output->data;
	sway_layer->output_destroy.notify = handle_output_destroy;
	wl_signal_add(&output->events.destroy, &sway_layer->output_destroy);

	wl_list_insert(&output->layers[layer_surface->client_pending.layer],
			&sway_layer->link);

	// Temporarily set the layer's current state to client_pending
	// So that we can easily arrange it
	struct wlr_layer_surface_v1_state old_state = layer_surface->current;
	layer_surface->current = layer_surface->client_pending;
	arrange_layers(output);
	layer_surface->current = old_state;
}
