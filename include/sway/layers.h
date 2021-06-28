#ifndef _SWAY_LAYERS_H
#define _SWAY_LAYERS_H
#include <stdbool.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_layer_shell_v1.h>

enum layer_parent {
	LAYER_PARENT_LAYER,
	LAYER_PARENT_POPUP,
};

struct sway_layer_surface {
	struct wlr_layer_surface_v1 *layer_surface;
	struct wl_list link;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
	struct wl_listener output_destroy;
	struct wl_listener new_popup;
	struct wl_listener new_subsurface;

	struct wlr_box geo;
	enum zwlr_layer_shell_v1_layer layer;
};

struct sway_layer_popup {
	struct wlr_xdg_popup *wlr_popup;
	enum layer_parent parent_type;
	union {
		struct sway_layer_surface *parent_layer;
		struct sway_layer_popup *parent_popup;
	};
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener new_popup;
};

struct sway_layer_subsurface {
	struct wlr_subsurface *wlr_subsurface;
	struct sway_layer_surface *layer_surface;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener layer_surface_destroy;
};

struct sway_output;
void arrange_layers(struct sway_output *output);

struct sway_layer_surface *layer_from_wlr_layer_surface_v1(
	struct wlr_layer_surface_v1 *layer_surface);

#endif
