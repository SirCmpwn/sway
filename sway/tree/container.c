#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output_layout.h>
#include "cairo_util.h"
#include "pango.h"
#include "sway/config.h"
#include "sway/desktop.h"
#include "sway/desktop/transaction.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

struct sway_container *container_create(struct sway_view *view) {
	struct sway_container *c = calloc(1, sizeof(struct sway_container));
	if (!c) {
		sway_log(SWAY_ERROR, "Unable to allocate sway_container");
		return NULL;
	}
	node_init(&c->node, N_CONTAINER, c);
	c->pending.layout = L_NONE;
	c->view = view;
	c->alpha = 1.0f;

	if (!view) {
		c->pending.children = create_list();
		c->current.children = create_list();
	}
	c->marks = create_list();
	c->outputs = create_list();

	wl_signal_init(&c->events.destroy);
	wl_signal_emit(&root->events.new_node, &c->node);

	return c;
}

void container_destroy(struct sway_container *con) {
	if (!sway_assert(con->node.destroying,
				"Tried to free container which wasn't marked as destroying")) {
		return;
	}
	if (!sway_assert(con->node.ntxnrefs == 0, "Tried to free container "
				"which is still referenced by transactions")) {
		return;
	}
	free(con->title);
	free(con->formatted_title);
	wlr_texture_destroy(con->title_focused);
	wlr_texture_destroy(con->title_focused_inactive);
	wlr_texture_destroy(con->title_unfocused);
	wlr_texture_destroy(con->title_urgent);
	list_free(con->pending.children);
	list_free(con->current.children);
	list_free(con->outputs);

	list_free_items_and_destroy(con->marks);
	wlr_texture_destroy(con->marks_focused);
	wlr_texture_destroy(con->marks_focused_inactive);
	wlr_texture_destroy(con->marks_unfocused);
	wlr_texture_destroy(con->marks_urgent);

	if (con->view) {
		if (con->view->container == con) {
			con->view->container = NULL;
		}
		if (con->view->destroying) {
			view_destroy(con->view);
		}
	}

	free(con);
}

void container_begin_destroy(struct sway_container *con) {
	if (con->view) {
		ipc_event_window(con, "close");
	}
	// The workspace must have the fullscreen pointer cleared so that the
	// seat code can find an appropriate new focus.
	if (con->pending.fullscreen_mode == FULLSCREEN_WORKSPACE && con->pending.workspace) {
		con->pending.workspace->fullscreen = NULL;
	}
	if (con->scratchpad && con->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		container_fullscreen_disable(con);
	}

	wl_signal_emit(&con->node.events.destroy, &con->node);

	container_end_mouse_operation(con);

	con->node.destroying = true;
	node_set_dirty(&con->node);

	if (con->scratchpad) {
		root_scratchpad_remove_container(con);
	}

	if (con->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		container_fullscreen_disable(con);
	}

	if (con->pending.parent || con->pending.workspace) {
		container_detach(con);
	}
}

void container_reap_empty(struct sway_container *con) {
	if (con->view) {
		return;
	}
	struct sway_workspace *ws = con->pending.workspace;
	while (con) {
		if (con->pending.children->length) {
			return;
		}
		struct sway_container *parent = con->pending.parent;
		container_begin_destroy(con);
		con = parent;
	}
	if (ws) {
		workspace_consider_destroy(ws);
	}
}

struct sway_container *container_flatten(struct sway_container *container) {
	if (container->view) {
		return NULL;
	}
	while (container && container->pending.children->length == 1) {
		struct sway_container *child = container->pending.children->items[0];
		struct sway_container *parent = container->pending.parent;
		container_replace(container, child);
		container_begin_destroy(container);
		container = parent;
	}
	return container;
}

struct sway_container *container_find_child(struct sway_container *container,
		bool (*test)(struct sway_container *con, void *data), void *data) {
	if (!container->pending.children) {
		return NULL;
	}
	for (int i = 0; i < container->pending.children->length; ++i) {
		struct sway_container *child = container->pending.children->items[i];
		if (test(child, data)) {
			return child;
		}
		struct sway_container *res = container_find_child(child, test, data);
		if (res) {
			return res;
		}
	}
	return NULL;
}

static struct sway_container *surface_at_view(struct sway_container *con, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	if (!sway_assert(con->view, "Expected a view")) {
		return NULL;
	}
	struct sway_view *view = con->view;
	double view_sx = lx - con->surface_x + view->geometry.x;
	double view_sy = ly - con->surface_y + view->geometry.y;

	double _sx, _sy;
	struct wlr_surface *_surface = NULL;
	switch (view->type) {
#if HAVE_XWAYLAND
	case SWAY_VIEW_XWAYLAND:
		_surface = wlr_surface_surface_at(view->surface,
				view_sx, view_sy, &_sx, &_sy);
		break;
#endif
	case SWAY_VIEW_XDG_SHELL:
		_surface = wlr_xdg_surface_surface_at(
				view->wlr_xdg_surface,
				view_sx, view_sy, &_sx, &_sy);
		break;
	}
	if (_surface) {
		*sx = _sx;
		*sy = _sy;
		*surface = _surface;
		return con;
	}
	return NULL;
}

/**
 * container_at for a container with layout L_TABBED.
 */
static struct sway_container *container_at_tabbed(struct sway_node *parent,
		double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct wlr_box box;
	node_get_box(parent, &box);
	if (lx < box.x || lx > box.x + box.width ||
			ly < box.y || ly > box.y + box.height) {
		return NULL;
	}
	struct sway_seat *seat = input_manager_current_seat();
	list_t *children = node_get_children(parent);
	if (!children->length) {
		return NULL;
	}

	// Tab titles
	int title_height = container_titlebar_height();
	if (ly < box.y + title_height) {
		int tab_width = box.width / children->length;
		int child_index = (lx - box.x) / tab_width;
		if (child_index >= children->length) {
			child_index = children->length - 1;
		}
		struct sway_container *child = children->items[child_index];
		return child;
	}

	// Surfaces
	struct sway_node *current = seat_get_active_tiling_child(seat, parent);
	return current ? tiling_container_at(current, lx, ly, surface, sx, sy) : NULL;
}

/**
 * container_at for a container with layout L_STACKED.
 */
static struct sway_container *container_at_stacked(struct sway_node *parent,
		double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct wlr_box box;
	node_get_box(parent, &box);
	if (lx < box.x || lx > box.x + box.width ||
			ly < box.y || ly > box.y + box.height) {
		return NULL;
	}
	struct sway_seat *seat = input_manager_current_seat();
	list_t *children = node_get_children(parent);

	// Title bars
	int title_height = container_titlebar_height();
	if (title_height > 0) {
		int child_index = (ly - box.y) / title_height;
		if (child_index < children->length) {
			struct sway_container *child = children->items[child_index];
			return child;
		}
	}

	// Surfaces
	struct sway_node *current = seat_get_active_tiling_child(seat, parent);
	return current ? tiling_container_at(current, lx, ly, surface, sx, sy) : NULL;
}

/**
 * container_at for a container with layout L_HORIZ or L_VERT.
 */
static struct sway_container *container_at_linear(struct sway_node *parent,
		double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	list_t *children = node_get_children(parent);
	for (int i = 0; i < children->length; ++i) {
		struct sway_container *child = children->items[i];
		struct sway_container *container =
			tiling_container_at(&child->node, lx, ly, surface, sx, sy);
		if (container) {
			return container;
		}
	}
	return NULL;
}

static struct sway_container *floating_container_at(double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	// For outputs with floating containers that overhang the output bounds,
	// those at the end of the output list appear on top of floating
	// containers from other outputs, so iterate the list in reverse.
	for (int i = root->outputs->length - 1; i >= 0; --i) {
		struct sway_output *output = root->outputs->items[i];
		for (int j = 0; j < output->workspaces->length; ++j) {
			struct sway_workspace *ws = output->workspaces->items[j];
			if (!workspace_is_visible(ws)) {
				continue;
			}
			// Items at the end of the list are on top, so iterate the list in
			// reverse.
			for (int k = ws->floating->length - 1; k >= 0; --k) {
				struct sway_container *floater = ws->floating->items[k];
				struct sway_container *container =
					tiling_container_at(&floater->node, lx, ly, surface, sx, sy);
				if (container) {
					return container;
				}
			}
		}
	}
	return NULL;
}

static struct sway_container *view_container_content_at(struct sway_node *parent,
		double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	if (!sway_assert(node_is_view(parent), "Expected a view")) {
		return NULL;
	}

	struct sway_container *container = parent->sway_container;
	struct wlr_box box = {
			.x = container->pending.content_x,
			.y = container->pending.content_y,
			.width = container->pending.content_width,
			.height = container->pending.content_height,
	};

	if (wlr_box_contains_point(&box, lx, ly)) {
		surface_at_view(parent->sway_container, lx, ly, surface, sx, sy);
		return container;
	}

	return NULL;
}

static struct sway_container *view_container_at(struct sway_node *parent,
		double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	if (!sway_assert(node_is_view(parent), "Expected a view")) {
		return NULL;
	}

	struct sway_container *container = parent->sway_container;
	struct wlr_box box = {
			.x = container->pending.x,
			.y = container->pending.y,
			.width = container->pending.width,
			.height = container->pending.height,
	};

	if (wlr_box_contains_point(&box, lx, ly)) {
		surface_at_view(parent->sway_container, lx, ly, surface, sx, sy);
		return container;
	}

	return NULL;
}

struct sway_container *tiling_container_at(struct sway_node *parent,
		double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	if (node_is_view(parent)) {
		return view_container_at(parent, lx, ly, surface, sx, sy);
	}
	if (!node_get_children(parent)) {
		return NULL;
	}
	switch (node_get_layout(parent)) {
	case L_HORIZ:
	case L_VERT:
		return container_at_linear(parent, lx, ly, surface, sx, sy);
	case L_TABBED:
		return container_at_tabbed(parent, lx, ly, surface, sx, sy);
	case L_STACKED:
		return container_at_stacked(parent, lx, ly, surface, sx, sy);
	case L_NONE:
		return NULL;
	}
	return NULL;
}

static bool surface_is_popup(struct wlr_surface *surface) {
	if (wlr_surface_is_xdg_surface(surface)) {
		struct wlr_xdg_surface *xdg_surface =
			wlr_xdg_surface_from_wlr_surface(surface);
		while (xdg_surface && xdg_surface->role != WLR_XDG_SURFACE_ROLE_NONE) {
			if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
				return true;
			}
			xdg_surface = xdg_surface->toplevel->parent;
		}
		return false;
	}

	return false;
}

struct sway_container *container_at(struct sway_workspace *workspace,
		double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct sway_container *c;

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_container *focus = seat_get_focused_container(seat);
	bool is_floating = focus && container_is_floating_or_child(focus);
	// Focused view's popups
	if (focus && focus->view) {
		c = surface_at_view(focus, lx, ly, surface, sx, sy);
		if (c && surface_is_popup(*surface)) {
			return c;
		}
		*surface = NULL;
	}
	// Floating
	if ((c = floating_container_at(lx, ly, surface ,sx ,sy))) {
		return c;
	}
	// Tiling (focused)
	if (focus && focus->view && !is_floating) {
		if ((c = view_container_content_at(&focus->node, lx, ly, surface, sx, sy))) {
			return c;
		}
	}
	// Tiling (non-focused)
	if ((c = tiling_container_at(&workspace->node, lx, ly, surface, sx, sy))) {
		return c;
	}
	return NULL;
}

void container_for_each_child(struct sway_container *container,
		void (*f)(struct sway_container *container, void *data),
		void *data) {
	if (container->pending.children)  {
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct sway_container *child = container->pending.children->items[i];
			f(child, data);
			container_for_each_child(child, f, data);
		}
	}
}

struct sway_container *container_obstructing_fullscreen_container(struct sway_container *container)
{
	struct sway_workspace *workspace = container->pending.workspace;

	if (workspace && workspace->fullscreen && !container_is_fullscreen_or_child(container)) {
		if (container_is_transient_for(container, workspace->fullscreen)) {
			return NULL;
		}
		return workspace->fullscreen;
	}

	struct sway_container *fullscreen_global = root->fullscreen_global;
	if (fullscreen_global && container != fullscreen_global && !container_has_ancestor(container, fullscreen_global)) {
		if (container_is_transient_for(container, fullscreen_global)) {
			return NULL;
		}
		return fullscreen_global;
	}

	return NULL;
}

bool container_has_ancestor(struct sway_container *descendant,
		struct sway_container *ancestor) {
	while (descendant) {
		descendant = descendant->pending.parent;
		if (descendant == ancestor) {
			return true;
		}
	}
	return false;
}

void container_damage_whole(struct sway_container *container) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_damage_whole_container(output, container);
	}
}

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct sway_output *container_get_effective_output(struct sway_container *con) {
	if (con->outputs->length == 0) {
		return NULL;
	}
	return con->outputs->items[con->outputs->length - 1];
}

static void update_title_texture(struct sway_container *con,
		struct wlr_texture **texture, struct border_colors *class) {
	struct sway_output *output = container_get_effective_output(con);
	if (!output) {
		return;
	}
	if (*texture) {
		wlr_texture_destroy(*texture);
		*texture = NULL;
	}
	if (!con->formatted_title) {
		return;
	}

	double scale = output->wlr_output->scale;
	int width = 0;
	int height = con->title_height * scale;

	// We must use a non-nil cairo_t for cairo_set_font_options to work.
	// Therefore, we cannot use cairo_create(NULL).
	cairo_surface_t *dummy_surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, 0, 0);
	cairo_t *c = cairo_create(dummy_surface);
	cairo_set_antialias(c, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	if (output->wlr_output->subpixel == WL_OUTPUT_SUBPIXEL_NONE) {
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
	} else {
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
		cairo_font_options_set_subpixel_order(fo,
			to_cairo_subpixel_order(output->wlr_output->subpixel));
	}
	cairo_set_font_options(c, fo);
	get_text_size(c, config->font, &width, NULL, NULL, scale,
			config->pango_markup, "%s", con->formatted_title);
	cairo_surface_destroy(dummy_surface);
	cairo_destroy(c);

	if (width == 0 || height == 0) {
		return;
	}

	cairo_surface_t *surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, width, height);
	cairo_t *cairo = cairo_create(surface);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_set_font_options(cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_set_source_rgba(cairo, class->background[0], class->background[1],
			class->background[2], class->background[3]);
	cairo_paint(cairo);
	PangoContext *pango = pango_cairo_create_context(cairo);
	cairo_set_source_rgba(cairo, class->text[0], class->text[1],
			class->text[2], class->text[3]);
	cairo_move_to(cairo, 0, 0);

	pango_printf(cairo, config->font, scale, config->pango_markup,
			"%s", con->formatted_title);

	cairo_surface_flush(surface);
	unsigned char *data = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	struct wlr_renderer *renderer = wlr_backend_get_renderer(
			output->wlr_output->backend);
	*texture = wlr_texture_from_pixels(
			renderer, DRM_FORMAT_ARGB8888, stride, width, height, data);
	cairo_surface_destroy(surface);
	g_object_unref(pango);
	cairo_destroy(cairo);
}

void container_update_title_textures(struct sway_container *container) {
	update_title_texture(container, &container->title_focused,
			&config->border_colors.focused);
	update_title_texture(container, &container->title_focused_inactive,
			&config->border_colors.focused_inactive);
	update_title_texture(container, &container->title_unfocused,
			&config->border_colors.unfocused);
	update_title_texture(container, &container->title_urgent,
			&config->border_colors.urgent);
	container_damage_whole(container);
}

void container_calculate_title_height(struct sway_container *container) {
        int height;
        int baseline;

	if (!container->formatted_title) {
		container->title_height = 0;
		return;
	}
	if (config->titlebar_v_height >= 0) {
		height   = config->titlebar_v_height;
		baseline = round(height * 0.77);
	} else {
		cairo_t *cairo = cairo_create(NULL);
		get_text_size(cairo, config->font, NULL, &height, &baseline, 1,
				config->pango_markup, "%s", container->formatted_title);
		cairo_destroy(cairo);
	}
	container->title_height = height;
	container->title_baseline = baseline;
}

/**
 * Calculate and return the length of the tree representation.
 * An example tree representation is: V[Terminal, Firefox]
 * If buffer is not NULL, also populate the buffer with the representation.
 */
size_t container_build_representation(enum sway_container_layout layout,
		list_t *children, char *buffer) {
	size_t len = 2;
	switch (layout) {
	case L_VERT:
		lenient_strcat(buffer, "V[");
		break;
	case L_HORIZ:
		lenient_strcat(buffer, "H[");
		break;
	case L_TABBED:
		lenient_strcat(buffer, "T[");
		break;
	case L_STACKED:
		lenient_strcat(buffer, "S[");
		break;
	case L_NONE:
		lenient_strcat(buffer, "D[");
		break;
	}
	for (int i = 0; i < children->length; ++i) {
		if (i != 0) {
			++len;
			lenient_strcat(buffer, " ");
		}
		struct sway_container *child = children->items[i];
		const char *identifier = NULL;
		if (child->view) {
			identifier = view_get_class(child->view);
			if (!identifier) {
				identifier = view_get_app_id(child->view);
			}
		} else {
			identifier = child->formatted_title;
		}
		if (identifier) {
			len += strlen(identifier);
			lenient_strcat(buffer, identifier);
		} else {
			len += 6;
			lenient_strcat(buffer, "(null)");
		}
	}
	++len;
	lenient_strcat(buffer, "]");
	return len;
}

void container_update_representation(struct sway_container *con) {
	if (!con->view) {
		size_t len = container_build_representation(con->pending.layout,
				con->pending.children, NULL);
		free(con->formatted_title);
		con->formatted_title = calloc(len + 1, sizeof(char));
		if (!sway_assert(con->formatted_title,
					"Unable to allocate title string")) {
			return;
		}
		container_build_representation(con->pending.layout, con->pending.children,
				con->formatted_title);
		container_calculate_title_height(con);
		container_update_title_textures(con);
	}
	if (con->pending.parent) {
		container_update_representation(con->pending.parent);
	} else if (con->pending.workspace) {
		workspace_update_representation(con->pending.workspace);
	}
}

size_t container_titlebar_height(void) {
	return config->font_height + config->titlebar_v_padding * 2;
}

void floating_calculate_constraints(int *min_width, int *max_width,
		int *min_height, int *max_height) {
	if (config->floating_minimum_width == -1) { // no minimum
		*min_width = 0;
	} else if (config->floating_minimum_width == 0) { // automatic
		*min_width = 75;
	} else {
		*min_width = config->floating_minimum_width;
	}

	if (config->floating_minimum_height == -1) { // no minimum
		*min_height = 0;
	} else if (config->floating_minimum_height == 0) { // automatic
		*min_height = 50;
	} else {
		*min_height = config->floating_minimum_height;
	}

	struct wlr_box *box = wlr_output_layout_get_box(root->output_layout, NULL);

	if (config->floating_maximum_width == -1) { // no maximum
		*max_width = INT_MAX;
	} else if (config->floating_maximum_width == 0) { // automatic
		*max_width = box->width;
	} else {
		*max_width = config->floating_maximum_width;
	}

	if (config->floating_maximum_height == -1) { // no maximum
		*max_height = INT_MAX;
	} else if (config->floating_maximum_height == 0) { // automatic
		*max_height = box->height;
	} else {
		*max_height = config->floating_maximum_height;
	}

}

static void floating_natural_resize(struct sway_container *con) {
	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	if (!con->view) {
		con->pending.width = fmax(min_width, fmin(con->pending.width, max_width));
		con->pending.height = fmax(min_height, fmin(con->pending.height, max_height));
	} else {
		struct sway_view *view = con->view;
		con->pending.content_width =
			fmax(min_width, fmin(view->natural_width, max_width));
		con->pending.content_height =
			fmax(min_height, fmin(view->natural_height, max_height));
		container_set_geometry_from_content(con);
	}
}

void container_floating_resize_and_center(struct sway_container *con) {
	struct sway_workspace *ws = con->pending.workspace;
	if (!ws) {
		// On scratchpad, just resize
		floating_natural_resize(con);
		return;
	}

	struct wlr_box *ob = wlr_output_layout_get_box(root->output_layout,
			ws->output->wlr_output);
	if (!ob) {
		// On NOOP output. Will be called again when moved to an output
		con->pending.x = 0;
		con->pending.y = 0;
		con->pending.width = 0;
		con->pending.height = 0;
		return;
	}

	floating_natural_resize(con);
	if (!con->view) {
		if (con->pending.width > ws->width || con->pending.height > ws->height) {
			con->pending.x = ob->x + (ob->width - con->pending.width) / 2;
			con->pending.y = ob->y + (ob->height - con->pending.height) / 2;
		} else {
			con->pending.x = ws->x + (ws->width - con->pending.width) / 2;
			con->pending.y = ws->y + (ws->height - con->pending.height) / 2;
		}
	} else {
		if (con->pending.content_width > ws->width
				|| con->pending.content_height > ws->height) {
			con->pending.content_x = ob->x + (ob->width - con->pending.content_width) / 2;
			con->pending.content_y = ob->y + (ob->height - con->pending.content_height) / 2;
		} else {
			con->pending.content_x = ws->x + (ws->width - con->pending.content_width) / 2;
			con->pending.content_y = ws->y + (ws->height - con->pending.content_height) / 2;
		}

		// If the view's border is B_NONE then these properties are ignored.
		con->pending.border_top = con->pending.border_bottom = true;
		con->pending.border_left = con->pending.border_right = true;

		container_set_geometry_from_content(con);
	}
}

void container_floating_set_default_size(struct sway_container *con) {
	if (!sway_assert(con->pending.workspace, "Expected a container on a workspace")) {
		return;
	}

	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	struct wlr_box *box = calloc(1, sizeof(struct wlr_box));
	workspace_get_box(con->pending.workspace, box);

	double width = fmax(min_width, fmin(box->width * 0.5, max_width));
	double height = fmax(min_height, fmin(box->height * 0.75, max_height));
	if (!con->view) {
		con->pending.width = width;
		con->pending.height = height;
	} else {
		con->pending.content_width = width;
		con->pending.content_height = height;
		container_set_geometry_from_content(con);
	}

	free(box);
}


/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void container_set_resizing(struct sway_container *con, bool resizing) {
	if (!con) {
		return;
	}

	if (con->view) {
		if (con->view->impl->set_resizing) {
			con->view->impl->set_resizing(con->view, resizing);
		}
	} else {
		for (int i = 0; i < con->pending.children->length; ++i ) {
			struct sway_container *child = con->pending.children->items[i];
			container_set_resizing(child, resizing);
		}
	}
}

void container_set_floating(struct sway_container *container, bool enable) {
	if (container_is_floating(container) == enable) {
		return;
	}

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_workspace *workspace = container->pending.workspace;
	struct sway_container *focus = seat_get_focused_container(seat);
	bool set_focus = focus == container;

	if (enable) {
		struct sway_container *old_parent = container->pending.parent;
		container_detach(container);
		workspace_add_floating(workspace, container);
		if (container->view) {
			view_set_tiled(container->view, false);
			if (container->view->using_csd) {
				container->pending.border = B_CSD;
			}
		}
		container_floating_set_default_size(container);
		container_floating_resize_and_center(container);
		if (old_parent) {
			if (set_focus) {
				seat_set_raw_focus(seat, &old_parent->node);
				seat_set_raw_focus(seat, &container->node);
			}
			container_reap_empty(old_parent);
		}
	} else {
		// Returning to tiled
		if (container->scratchpad) {
			root_scratchpad_remove_container(container);
		}
		container_detach(container);
		struct sway_container *reference =
			seat_get_focus_inactive_tiling(seat, workspace);
		if (reference) {
			if (reference->view) {
				container_add_sibling(reference, container, 1);
			} else {
				container_add_child(reference, container);
			}
			container->pending.width = reference->pending.width;
			container->pending.height = reference->pending.height;
		} else {
			struct sway_container *other =
				workspace_add_tiling(workspace, container);
			other->pending.width = workspace->width;
			other->pending.height = workspace->height;
		}
		if (container->view) {
			view_set_tiled(container->view, true);
			if (container->view->using_csd) {
				container->pending.border = container->saved_border;
			}
		}
		container->width_fraction = 0;
		container->height_fraction = 0;
	}

	container_end_mouse_operation(container);

	ipc_event_window(container, "floating");
}

void container_set_geometry_from_content(struct sway_container *con) {
	if (!sway_assert(con->view, "Expected a view")) {
		return;
	}
	if (!sway_assert(container_is_floating(con), "Expected a floating view")) {
		return;
	}
	size_t border_width = 0;
	size_t top = 0;

	if (con->pending.border != B_CSD) {
		border_width = con->pending.border_thickness * (con->pending.border != B_NONE);
		top = con->pending.border == B_NORMAL ?
			container_titlebar_height() : border_width;
	}

	con->pending.x = con->pending.content_x - border_width;
	con->pending.y = con->pending.content_y - top;
	con->pending.width = con->pending.content_width + border_width * 2;
	con->pending.height = top + con->pending.content_height + border_width;
	node_set_dirty(&con->node);
}

bool container_is_floating(struct sway_container *container) {
	if (!container->pending.parent && container->pending.workspace &&
			list_find(container->pending.workspace->floating, container) != -1) {
		return true;
	}
	if (container->scratchpad) {
		return true;
	}
	return false;
}

bool container_is_current_floating(struct sway_container *container) {
	if (!container->current.parent && container->current.workspace &&
			list_find(container->current.workspace->floating, container) != -1) {
		return true;
	}
	if (container->scratchpad) {
		return true;
	}
	return false;
}

void container_get_box(struct sway_container *container, struct wlr_box *box) {
	box->x = container->pending.x;
	box->y = container->pending.y;
	box->width = container->pending.width;
	box->height = container->pending.height;
}

/**
 * Translate the container's position as well as all children.
 */
void container_floating_translate(struct sway_container *con,
		double x_amount, double y_amount) {
	con->pending.x += x_amount;
	con->pending.y += y_amount;
	con->pending.content_x += x_amount;
	con->pending.content_y += y_amount;

	if (con->pending.children) {
		for (int i = 0; i < con->pending.children->length; ++i) {
			struct sway_container *child = con->pending.children->items[i];
			container_floating_translate(child, x_amount, y_amount);
		}
	}

	node_set_dirty(&con->node);
}

/**
 * Choose an output for the floating container's new position.
 *
 * If the center of the container intersects an output then we'll choose that
 * one, otherwise we'll choose whichever output is closest to the container's
 * center.
 */
struct sway_output *container_floating_find_output(struct sway_container *con) {
	double center_x = con->pending.x + con->pending.width / 2;
	double center_y = con->pending.y + con->pending.height / 2;
	struct sway_output *closest_output = NULL;
	double closest_distance = DBL_MAX;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		double closest_x, closest_y;
		output_get_box(output, &output_box);
		wlr_box_closest_point(&output_box, center_x, center_y,
				&closest_x, &closest_y);
		if (center_x == closest_x && center_y == closest_y) {
			// The center of the floating container is on this output
			return output;
		}
		double x_dist = closest_x - center_x;
		double y_dist = closest_y - center_y;
		double distance = x_dist * x_dist + y_dist * y_dist;
		if (distance < closest_distance) {
			closest_output = output;
			closest_distance = distance;
		}
	}
	return closest_output;
}

void container_floating_move_to(struct sway_container *con,
		double lx, double ly) {
	if (!sway_assert(container_is_floating(con),
			"Expected a floating container")) {
		return;
	}
	container_floating_translate(con, lx - con->pending.x, ly - con->pending.y);
	if (container_is_scratchpad_hidden(con)) {
		return;
	}
	struct sway_workspace *old_workspace = con->pending.workspace;
	struct sway_output *new_output = container_floating_find_output(con);
	if (!sway_assert(new_output, "Unable to find any output")) {
		return;
	}
	struct sway_workspace *new_workspace =
		output_get_active_workspace(new_output);
	if (new_workspace && old_workspace != new_workspace) {
		container_detach(con);
		workspace_add_floating(new_workspace, con);
		arrange_workspace(old_workspace);
		arrange_workspace(new_workspace);
		workspace_detect_urgent(old_workspace);
		workspace_detect_urgent(new_workspace);
	}
}

void container_floating_move_to_center(struct sway_container *con) {
	if (!sway_assert(container_is_floating(con),
			"Expected a floating container")) {
		return;
	}
	struct sway_workspace *ws = con->pending.workspace;
	double new_lx = ws->x + (ws->width - con->pending.width) / 2;
	double new_ly = ws->y + (ws->height - con->pending.height) / 2;
	container_floating_translate(con, new_lx - con->pending.x, new_ly - con->pending.y);
}

static bool find_urgent_iterator(struct sway_container *con, void *data) {
	return con->view && view_is_urgent(con->view);
}

bool container_has_urgent_child(struct sway_container *container) {
	return container_find_child(container, find_urgent_iterator, NULL);
}

void container_end_mouse_operation(struct sway_container *container) {
	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seatop_unref(seat, container);
	}
}

static void set_fullscreen(struct sway_container *con, bool enable) {
	if (!con->view) {
		return;
	}
	if (con->view->impl->set_fullscreen) {
		con->view->impl->set_fullscreen(con->view, enable);
		if (con->view->foreign_toplevel) {
			wlr_foreign_toplevel_handle_v1_set_fullscreen(
				con->view->foreign_toplevel, enable);
		}
	}
}

static void container_fullscreen_workspace(struct sway_container *con) {
	if (!sway_assert(con->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}
	set_fullscreen(con, true);
	con->pending.fullscreen_mode = FULLSCREEN_WORKSPACE;

	con->saved_x = con->pending.x;
	con->saved_y = con->pending.y;
	con->saved_width = con->pending.width;
	con->saved_height = con->pending.height;

	if (con->pending.workspace) {
		con->pending.workspace->fullscreen = con;
		struct sway_seat *seat;
		struct sway_workspace *focus_ws;
		wl_list_for_each(seat, &server.input->seats, link) {
			focus_ws = seat_get_focused_workspace(seat);
			if (focus_ws == con->pending.workspace) {
				seat_set_focus_container(seat, con);
			} else {
				struct sway_node *focus =
					seat_get_focus_inactive(seat, &root->node);
				seat_set_raw_focus(seat, &con->node);
				seat_set_raw_focus(seat, focus);
			}
		}
	}

	container_end_mouse_operation(con);
	ipc_event_window(con, "fullscreen_mode");
}

static void container_fullscreen_global(struct sway_container *con) {
	if (!sway_assert(con->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}
	set_fullscreen(con, true);

	root->fullscreen_global = con;
	con->saved_x = con->pending.x;
	con->saved_y = con->pending.y;
	con->saved_width = con->pending.width;
	con->saved_height = con->pending.height;

	struct sway_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct sway_container *focus = seat_get_focused_container(seat);
		if (focus && focus != con) {
			seat_set_focus_container(seat, con);
		}
	}

	con->pending.fullscreen_mode = FULLSCREEN_GLOBAL;
	container_end_mouse_operation(con);
	ipc_event_window(con, "fullscreen_mode");
}

void container_fullscreen_disable(struct sway_container *con) {
	if (!sway_assert(con->pending.fullscreen_mode != FULLSCREEN_NONE,
				"Expected a fullscreen container")) {
		return;
	}
	set_fullscreen(con, false);

	if (container_is_floating(con)) {
		con->pending.x = con->saved_x;
		con->pending.y = con->saved_y;
		con->pending.width = con->saved_width;
		con->pending.height = con->saved_height;
	}

	if (con->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		if (con->pending.workspace) {
			con->pending.workspace->fullscreen = NULL;
			if (container_is_floating(con)) {
				struct sway_output *output =
					container_floating_find_output(con);
				if (con->pending.workspace->output != output) {
					container_floating_move_to_center(con);
				}
			}
		}
	} else {
		root->fullscreen_global = NULL;
	}

	// If the container was mapped as fullscreen and set as floating by
	// criteria, it needs to be reinitialized as floating to get the proper
	// size and location
	if (container_is_floating(con) && (con->pending.width == 0 || con->pending.height == 0)) {
		container_floating_resize_and_center(con);
	}

	con->pending.fullscreen_mode = FULLSCREEN_NONE;
	container_end_mouse_operation(con);
	ipc_event_window(con, "fullscreen_mode");

	if (con->scratchpad) {
		struct sway_seat *seat;
		wl_list_for_each(seat, &server.input->seats, link) {
			struct sway_container *focus = seat_get_focused_container(seat);
			if (focus == con || container_has_ancestor(focus, con)) {
				seat_set_focus(seat,
						seat_get_focus_inactive(seat, &root->node));
			}
		}
	}
}

void container_set_fullscreen(struct sway_container *con,
		enum sway_fullscreen_mode mode) {
	if (con->pending.fullscreen_mode == mode) {
		return;
	}

	switch (mode) {
	case FULLSCREEN_NONE:
		container_fullscreen_disable(con);
		break;
	case FULLSCREEN_WORKSPACE:
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (con->pending.workspace && con->pending.workspace->fullscreen) {
			container_fullscreen_disable(con->pending.workspace->fullscreen);
		}
		container_fullscreen_workspace(con);
		break;
	case FULLSCREEN_GLOBAL:
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (con->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
			container_fullscreen_disable(con);
		}
		container_fullscreen_global(con);
		break;
	}
}

struct sway_container *container_toplevel_ancestor(
		struct sway_container *container) {
	while (container->pending.parent) {
		container = container->pending.parent;
	}

	return container;
}

bool container_is_floating_or_child(struct sway_container *container) {
	return container_is_floating(container_toplevel_ancestor(container));
}

bool container_is_fullscreen_or_child(struct sway_container *container) {
	do {
		if (container->pending.fullscreen_mode) {
			return true;
		}
		container = container->pending.parent;
	} while (container);

	return false;
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

void container_discover_outputs(struct sway_container *con) {
	struct wlr_box con_box = {
		.x = con->current.x,
		.y = con->current.y,
		.width = con->current.width,
		.height = con->current.height,
	};
	struct sway_output *old_output = container_get_effective_output(con);

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		output_get_box(output, &output_box);
		struct wlr_box intersection;
		bool intersects =
			wlr_box_intersection(&intersection, &con_box, &output_box);
		int index = list_find(con->outputs, output);

		if (intersects && index == -1) {
			// Send enter
			sway_log(SWAY_DEBUG, "Container %p entered output %p", con, output);
			if (con->view) {
				view_for_each_surface(con->view,
						surface_send_enter_iterator, output->wlr_output);
				if (con->view->foreign_toplevel) {
					wlr_foreign_toplevel_handle_v1_output_enter(
							con->view->foreign_toplevel, output->wlr_output);
				}
			}
			list_add(con->outputs, output);
		} else if (!intersects && index != -1) {
			// Send leave
			sway_log(SWAY_DEBUG, "Container %p left output %p", con, output);
			if (con->view) {
				view_for_each_surface(con->view,
					surface_send_leave_iterator, output->wlr_output);
				if (con->view->foreign_toplevel) {
					wlr_foreign_toplevel_handle_v1_output_leave(
							con->view->foreign_toplevel, output->wlr_output);
				}
			}
			list_del(con->outputs, index);
		}
	}
	struct sway_output *new_output = container_get_effective_output(con);
	double old_scale = old_output && old_output->enabled ?
		old_output->wlr_output->scale : -1;
	double new_scale = new_output ? new_output->wlr_output->scale : -1;
	if (old_scale != new_scale) {
		container_update_title_textures(con);
		container_update_marks_textures(con);
	}
}

enum sway_container_layout container_parent_layout(struct sway_container *con) {
	if (con->pending.parent) {
		return con->pending.parent->pending.layout;
	}
	if (con->pending.workspace) {
		return con->pending.workspace->layout;
	}
	return L_NONE;
}

enum sway_container_layout container_current_parent_layout(
		struct sway_container *con) {
	if (con->current.parent) {
		return con->current.parent->current.layout;
	}
	return con->current.workspace->current.layout;
}

list_t *container_get_siblings(struct sway_container *container) {
	if (container->pending.parent) {
		return container->pending.parent->pending.children;
	}
	if (container_is_scratchpad_hidden(container)) {
		return NULL;
	}
	if (list_find(container->pending.workspace->tiling, container) != -1) {
		return container->pending.workspace->tiling;
	}
	return container->pending.workspace->floating;
}

int container_sibling_index(struct sway_container *child) {
	return list_find(container_get_siblings(child), child);
}

list_t *container_get_current_siblings(struct sway_container *container) {
	if (container->current.parent) {
		return container->current.parent->current.children;
	}
	return container->current.workspace->current.tiling;
}

void container_handle_fullscreen_reparent(struct sway_container *con) {
	if (con->pending.fullscreen_mode != FULLSCREEN_WORKSPACE || !con->pending.workspace ||
			con->pending.workspace->fullscreen == con) {
		return;
	}
	if (con->pending.workspace->fullscreen) {
		container_fullscreen_disable(con->pending.workspace->fullscreen);
	}
	con->pending.workspace->fullscreen = con;

	arrange_workspace(con->pending.workspace);
}

static void set_workspace(struct sway_container *container, void *data) {
	container->pending.workspace = container->pending.parent->pending.workspace;
}

void container_insert_child(struct sway_container *parent,
		struct sway_container *child, int i) {
	if (child->pending.workspace) {
		container_detach(child);
	}
	list_insert(parent->pending.children, i, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_for_each_child(child, set_workspace, NULL);
	container_handle_fullscreen_reparent(child);
	container_update_representation(parent);
}

void container_add_sibling(struct sway_container *fixed,
		struct sway_container *active, bool after) {
	if (active->pending.workspace) {
		container_detach(active);
	}
	list_t *siblings = container_get_siblings(fixed);
	int index = list_find(siblings, fixed);
	list_insert(siblings, index + after, active);
	active->pending.parent = fixed->pending.parent;
	active->pending.workspace = fixed->pending.workspace;
	container_for_each_child(active, set_workspace, NULL);
	container_handle_fullscreen_reparent(active);
	container_update_representation(active);
}

void container_add_child(struct sway_container *parent,
		struct sway_container *child) {
	if (child->pending.workspace) {
		container_detach(child);
	}
	list_add(parent->pending.children, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_for_each_child(child, set_workspace, NULL);
	container_handle_fullscreen_reparent(child);
	container_update_representation(parent);
	node_set_dirty(&child->node);
	node_set_dirty(&parent->node);
}

void container_detach(struct sway_container *child) {
	if (child->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		child->pending.workspace->fullscreen = NULL;
	}
	if (child->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		root->fullscreen_global = NULL;
	}

	struct sway_container *old_parent = child->pending.parent;
	struct sway_workspace *old_workspace = child->pending.workspace;
	list_t *siblings = container_get_siblings(child);
	if (siblings) {
		int index = list_find(siblings, child);
		if (index != -1) {
			list_del(siblings, index);
		}
	}
	child->pending.parent = NULL;
	child->pending.workspace = NULL;
	container_for_each_child(child, set_workspace, NULL);

	if (old_parent) {
		container_update_representation(old_parent);
		node_set_dirty(&old_parent->node);
	} else if (old_workspace) {
		workspace_update_representation(old_workspace);
		node_set_dirty(&old_workspace->node);
	}
	node_set_dirty(&child->node);
}

void container_replace(struct sway_container *container,
		struct sway_container *replacement) {
	enum sway_fullscreen_mode fullscreen = container->pending.fullscreen_mode;
	bool scratchpad = container->scratchpad;
	struct sway_workspace *ws = NULL;
	if (fullscreen != FULLSCREEN_NONE) {
		container_fullscreen_disable(container);
	}
	if (scratchpad) {
		ws = container->pending.workspace;
		root_scratchpad_show(container);
		root_scratchpad_remove_container(container);
	}
	if (container->pending.parent || container->pending.workspace) {
		float width_fraction = container->width_fraction;
		float height_fraction = container->height_fraction;
		container_add_sibling(container, replacement, 1);
		container_detach(container);
		replacement->width_fraction = width_fraction;
		replacement->height_fraction = height_fraction;
	}
	if (scratchpad) {
		root_scratchpad_add_container(replacement, ws);
	}
	switch (fullscreen) {
	case FULLSCREEN_WORKSPACE:
		container_fullscreen_workspace(replacement);
		break;
	case FULLSCREEN_GLOBAL:
		container_fullscreen_global(replacement);
		break;
	case FULLSCREEN_NONE:
		// noop
		break;
	}
}

struct sway_container *container_split(struct sway_container *child,
		enum sway_container_layout layout) {
	// i3 doesn't split singleton H/V containers
	// https://github.com/i3/i3/blob/3cd1c45eba6de073bc4300eebb4e1cc1a0c4479a/src/tree.c#L354
	if (child->pending.parent || child->pending.workspace) {
		list_t *siblings = container_get_siblings(child);
		if (siblings->length == 1) {
			enum sway_container_layout current = container_parent_layout(child);
			if (container_is_floating(child)) {
				current = L_NONE;
			}
			if (current == L_HORIZ || current == L_VERT) {
				if (child->pending.parent) {
					child->pending.parent->pending.layout = layout;
					container_update_representation(child->pending.parent);
				} else {
					child->pending.workspace->layout = layout;
					workspace_update_representation(child->pending.workspace);
				}
				return child;
			}
		}
	}

	struct sway_seat *seat = input_manager_get_default_seat();
	bool set_focus = (seat_get_focus(seat) == &child->node);

	if (container_is_floating(child) && child->view) {
		view_set_tiled(child->view, true);
		if (child->view->using_csd) {
			child->pending.border = child->saved_border;
		}
	}

	struct sway_container *cont = container_create(NULL);
	cont->pending.width = child->pending.width;
	cont->pending.height = child->pending.height;
	cont->width_fraction = child->width_fraction;
	cont->height_fraction = child->height_fraction;
	cont->pending.x = child->pending.x;
	cont->pending.y = child->pending.y;
	cont->pending.layout = layout;

	container_replace(child, cont);
	container_add_child(cont, child);

	if (set_focus) {
		seat_set_raw_focus(seat, &cont->node);
		if (cont->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
			seat_set_focus(seat, &child->node);
		} else {
			seat_set_raw_focus(seat, &child->node);
		}
	}

	return cont;
}

bool container_is_transient_for(struct sway_container *child,
		struct sway_container *ancestor) {
	return config->popup_during_fullscreen == POPUP_SMART &&
		child->view && ancestor->view &&
		view_is_transient_for(child->view, ancestor->view);
}

static bool find_by_mark_iterator(struct sway_container *con, void *data) {
	char *mark = data;
	return container_has_mark(con, mark);
}

struct sway_container *container_find_mark(char *mark) {
	return root_find_container(find_by_mark_iterator, mark);
}

bool container_find_and_unmark(char *mark) {
	struct sway_container *con = root_find_container(
		find_by_mark_iterator, mark);
	if (!con) {
		return false;
	}

	for (int i = 0; i < con->marks->length; ++i) {
		char *con_mark = con->marks->items[i];
		if (strcmp(con_mark, mark) == 0) {
			free(con_mark);
			list_del(con->marks, i);
			container_update_marks_textures(con);
			ipc_event_window(con, "mark");
			return true;
		}
	}
	return false;
}

void container_clear_marks(struct sway_container *con) {
	for (int i = 0; i < con->marks->length; ++i) {
		free(con->marks->items[i]);
	}
	con->marks->length = 0;
	ipc_event_window(con, "mark");
}

bool container_has_mark(struct sway_container *con, char *mark) {
	for (int i = 0; i < con->marks->length; ++i) {
		char *item = con->marks->items[i];
		if (strcmp(item, mark) == 0) {
			return true;
		}
	}
	return false;
}

void container_add_mark(struct sway_container *con, char *mark) {
	list_add(con->marks, strdup(mark));
	ipc_event_window(con, "mark");
}

static void update_marks_texture(struct sway_container *con,
		struct wlr_texture **texture, struct border_colors *class) {
	struct sway_output *output = container_get_effective_output(con);
	if (!output) {
		return;
	}
	if (*texture) {
		wlr_texture_destroy(*texture);
		*texture = NULL;
	}
	if (!con->marks->length) {
		return;
	}

	size_t len = 0;
	for (int i = 0; i < con->marks->length; ++i) {
		char *mark = con->marks->items[i];
		if (mark[0] != '_') {
			len += strlen(mark) + 2;
		}
	}
	char *buffer = calloc(len + 1, 1);
	char *part = malloc(len + 1);

	if (!sway_assert(buffer && part, "Unable to allocate memory")) {
		free(buffer);
		return;
	}

	for (int i = 0; i < con->marks->length; ++i) {
		char *mark = con->marks->items[i];
		if (mark[0] != '_') {
			sprintf(part, "[%s]", mark);
			strcat(buffer, part);
		}
	}
	free(part);

	double scale = output->wlr_output->scale;
	int width = 0;
	int height = con->title_height * scale;

	cairo_t *c = cairo_create(NULL);
	get_text_size(c, config->font, &width, NULL, NULL, scale, false,
			"%s", buffer);
	cairo_destroy(c);

	if (width == 0 || height == 0) {
		return;
	}

	cairo_surface_t *surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, width, height);
	cairo_t *cairo = cairo_create(surface);
	cairo_set_source_rgba(cairo, class->background[0], class->background[1],
			class->background[2], class->background[3]);
	cairo_paint(cairo);
	PangoContext *pango = pango_cairo_create_context(cairo);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_set_source_rgba(cairo, class->text[0], class->text[1],
			class->text[2], class->text[3]);
	cairo_move_to(cairo, 0, 0);

	pango_printf(cairo, config->font, scale, false, "%s", buffer);

	cairo_surface_flush(surface);
	unsigned char *data = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	struct wlr_renderer *renderer = wlr_backend_get_renderer(
			output->wlr_output->backend);
	*texture = wlr_texture_from_pixels(
			renderer, DRM_FORMAT_ARGB8888, stride, width, height, data);
	cairo_surface_destroy(surface);
	g_object_unref(pango);
	cairo_destroy(cairo);
	free(buffer);
}

void container_update_marks_textures(struct sway_container *con) {
	if (!config->show_marks) {
		return;
	}
	update_marks_texture(con, &con->marks_focused,
			&config->border_colors.focused);
	update_marks_texture(con, &con->marks_focused_inactive,
			&config->border_colors.focused_inactive);
	update_marks_texture(con, &con->marks_unfocused,
			&config->border_colors.unfocused);
	update_marks_texture(con, &con->marks_urgent,
			&config->border_colors.urgent);
	container_damage_whole(con);
}

void container_raise_floating(struct sway_container *con) {
	// Bring container to front by putting it at the end of the floating list.
	struct sway_container *floater = container_toplevel_ancestor(con);
	if (container_is_floating(floater) && floater->pending.workspace) {
		list_move_to_end(floater->pending.workspace->floating, floater);
		node_set_dirty(&floater->pending.workspace->node);
	}
}

bool container_is_scratchpad_hidden(struct sway_container *con) {
	return con->scratchpad && !con->pending.workspace;
}

bool container_is_scratchpad_hidden_or_child(struct sway_container *con) {
	con = container_toplevel_ancestor(con);
	return con->scratchpad && !con->pending.workspace;
}

bool container_is_sticky(struct sway_container *con) {
	return con->is_sticky && container_is_floating(con);
}

bool container_is_sticky_or_child(struct sway_container *con) {
	return container_is_sticky(container_toplevel_ancestor(con));
}

static bool is_parallel(enum sway_container_layout first,
		enum sway_container_layout second) {
	switch (first) {
	case L_TABBED:
	case L_HORIZ:
		return second == L_TABBED || second == L_HORIZ;
	case L_STACKED:
	case L_VERT:
		return second == L_STACKED || second == L_VERT;
	default:
		return false;
	}
}

static bool container_is_squashable(struct sway_container *con,
		struct sway_container *child) {
	enum sway_container_layout gp_layout = container_parent_layout(con);
	return (con->pending.layout == L_HORIZ || con->pending.layout == L_VERT) &&
		(child->pending.layout == L_HORIZ || child->pending.layout == L_VERT) &&
		!is_parallel(con->pending.layout, child->pending.layout) &&
		is_parallel(gp_layout, child->pending.layout);
}

static void container_squash_children(struct sway_container *con) {
	for (int i = 0; i < con->pending.children->length; i++) {
		struct sway_container *child = con->pending.children->items[i];
		i += container_squash(child);
	}
}

int container_squash(struct sway_container *con) {
	if (!con->pending.children) {
		return 0;
	}
	if (con->pending.children->length != 1) {
		container_squash_children(con);
		return 0;
	}
	struct sway_container *child = con->pending.children->items[0];
	int idx = container_sibling_index(con);
	int change = 0;
	if (container_is_squashable(con, child)) {
		// con and child are a redundant H/V pair. Destroy them.
		while (child->pending.children->length) {
			struct sway_container *current = child->pending.children->items[0];
			container_detach(current);
			if (con->pending.parent) {
				container_insert_child(con->pending.parent, current, idx);
			} else {
				workspace_insert_tiling_direct(con->pending.workspace, current, idx);
			}
			change++;
		}
		// This will also destroy con because child was its only child
		container_reap_empty(child);
		change--;
	} else {
		container_squash_children(con);
	}
	return change;
}
