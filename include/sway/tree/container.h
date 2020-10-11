#ifndef _SWAY_CONTAINER_H
#define _SWAY_CONTAINER_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/render/color.h>
#include "list.h"
#include "sway/tree/node.h"

struct sway_view;
struct sway_seat;

enum sway_container_layout {
	L_NONE,
	L_HORIZ,
	L_VERT,
	L_STACKED,
	L_TABBED,
};

enum sway_container_border {
	B_NONE,
	B_PIXEL,
	B_NORMAL,
	B_CSD,
};

enum sway_fullscreen_mode {
	FULLSCREEN_NONE,
	FULLSCREEN_WORKSPACE,
	FULLSCREEN_GLOBAL,
};

struct sway_root;
struct sway_output;
struct sway_workspace;
struct sway_view;

enum wlr_direction;

struct sway_container_state {
	// Container properties
	enum sway_container_layout layout;
	double x, y;
	double width, height;

	enum sway_fullscreen_mode fullscreen_mode;

	struct sway_workspace *workspace;
	struct sway_container *parent;
	list_t *children;

	struct sway_container *focused_inactive_child;
	bool focused;

	enum sway_container_border border;
	int border_thickness;
	bool border_top;
	bool border_bottom;
	bool border_left;
	bool border_right;

	double content_x, content_y;
	double content_width, content_height;
};

struct sway_container {
	struct sway_node node;
	struct sway_view *view;

	// The pending state is the main container properties, and the current state is in the below struct.
	// This means most places of the code can refer to the main variables (pending state) and it'll just work.
	struct sway_container_state current;

	char *title;           // The view's title (unformatted)
	char *formatted_title; // The title displayed in the title bar

	enum sway_container_layout layout;
	enum sway_container_layout prev_split_layout;

	bool is_sticky;

	// For C_ROOT, this has no meaning
	// For other types, this is the position in layout coordinates
	// Includes borders
	double x, y;
	double width, height;
	double saved_x, saved_y;
	double saved_width, saved_height;

	// The share of the space of parent container this container occupies
	double width_fraction;
	double height_fraction;

	// The share of space of the parent container that all children occupy
	// Used for doing the resize calculations
	double child_total_width;
	double child_total_height;

	// These are in layout coordinates.
	double content_x, content_y;
	int content_width, content_height;

	// In most cases this is the same as the content x and y, but if the view
	// refuses to resize to the content dimensions then it can be smaller.
	// These are in layout coordinates.
	double surface_x, surface_y;

	enum sway_fullscreen_mode fullscreen_mode;

	enum sway_container_border border;

	// Used when the view changes to CSD unexpectedly. This will be a non-B_CSD
	// border which we use to restore when the view returns to SSD.
	enum sway_container_border saved_border;

	int border_thickness;
	bool border_top;
	bool border_bottom;
	bool border_left;
	bool border_right;

	struct sway_workspace *workspace; // NULL when hidden in the scratchpad
	struct sway_container *parent;    // NULL if container in root of workspace
	list_t *children;                 // struct sway_container

	// Outputs currently being intersected
	list_t *outputs; // struct sway_output

	// Indicates that the container is a scratchpad container.
	// Both hidden and visible scratchpad containers have scratchpad=true.
	// Hidden scratchpad containers have a NULL parent.
	bool scratchpad;

	float alpha;

	struct wlr_texture *title_focused;
	struct wlr_texture *title_focused_inactive;
	struct wlr_texture *title_unfocused;
	struct wlr_texture *title_urgent;
	size_t title_height;
	size_t title_baseline;

	list_t *marks; // char *
	struct wlr_texture *marks_focused;
	struct wlr_texture *marks_focused_inactive;
	struct wlr_texture *marks_unfocused;
	struct wlr_texture *marks_urgent;

	struct {
		struct wl_signal destroy;
	} events;
};

struct sway_container *container_create(struct sway_view *view);

void container_destroy(struct sway_container *con);

void container_begin_destroy(struct sway_container *con);

/**
 * Search a container's descendants a container based on test criteria. Returns
 * the first container that passes the test.
 */
struct sway_container *container_find_child(struct sway_container *container,
		bool (*test)(struct sway_container *view, void *data), void *data);

/**
 * Find a container at the given coordinates. Returns the surface and
 * surface-local coordinates of the given layout coordinates if the container
 * is a view and the view contains a surface at those coordinates.
 */
struct sway_container *container_at(struct sway_workspace *workspace,
		double lx, double ly, struct wlr_surface **surface,
		double *sx, double *sy);

struct sway_container *tiling_container_at(
		struct sway_node *parent, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);

void container_for_each_child(struct sway_container *container,
		void (*f)(struct sway_container *container, void *data), void *data);

/**
 * Returns true if the given container is an ancestor of this container.
 */
bool container_has_ancestor(struct sway_container *container,
		struct sway_container *ancestor);

void container_update_textures_recursive(struct sway_container *con);

void container_damage_whole(struct sway_container *container);

void container_reap_empty(struct sway_container *con);

struct sway_container *container_flatten(struct sway_container *container);

void container_update_title_textures(struct sway_container *container);

/**
 * Calculate the container's title_height property.
 */
void container_calculate_title_height(struct sway_container *container);

size_t container_build_representation(enum sway_container_layout layout,
		list_t *children, char *buffer);

void container_update_representation(struct sway_container *container);

/**
 * Return the height of a regular title bar.
 */
size_t container_titlebar_height(void);

void floating_calculate_constraints(int *min_width, int *max_width,
		int *min_height, int *max_height);

void container_floating_resize_and_center(struct sway_container *con);

void container_floating_set_default_size(struct sway_container *con);

void container_set_resizing(struct sway_container *con, bool resizing);

void container_set_floating(struct sway_container *container, bool enable);

void container_set_geometry_from_content(struct sway_container *con);

/**
 * Determine if the given container is itself floating.
 * This will return false for any descendants of a floating container.
 */
bool container_is_floating(struct sway_container *container);

/**
 * Get a container's box in layout coordinates.
 */
void container_get_box(struct sway_container *container, struct wlr_box *box);

/**
 * Move a floating container by the specified amount.
 */
void container_floating_translate(struct sway_container *con,
		double x_amount, double y_amount);

/**
 * Choose an output for the floating container's new position.
 */
struct sway_output *container_floating_find_output(struct sway_container *con);

/**
 * Move a floating container to a new layout-local position.
 */
void container_floating_move_to(struct sway_container *con,
		double lx, double ly);

/**
 * Move a floating container to the center of the workspace.
 */
void container_floating_move_to_center(struct sway_container *con);

bool container_has_urgent_child(struct sway_container *container);

/**
 * If the container is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void container_end_mouse_operation(struct sway_container *container);

void container_set_fullscreen(struct sway_container *con,
		enum sway_fullscreen_mode mode);

/**
 * Convenience function.
 */
void container_fullscreen_disable(struct sway_container *con);

/**
 * Walk up the container tree branch starting at the given container, and return
 * its earliest ancestor.
 */
struct sway_container *container_toplevel_ancestor(
		struct sway_container *container);

/**
 * Return true if the container is floating, or a child of a floating split
 * container.
 */
bool container_is_floating_or_child(struct sway_container *container);

/**
 * Return true if the container is fullscreen, or a child of a fullscreen split
 * container.
 */
bool container_is_fullscreen_or_child(struct sway_container *container);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct sway_output *container_get_effective_output(struct sway_container *con);

void container_discover_outputs(struct sway_container *con);

enum sway_container_layout container_parent_layout(struct sway_container *con);

enum sway_container_layout container_current_parent_layout(
		struct sway_container *con);

list_t *container_get_siblings(struct sway_container *container);

int container_sibling_index(struct sway_container *child);

list_t *container_get_current_siblings(struct sway_container *container);

void container_handle_fullscreen_reparent(struct sway_container *con);

void container_add_child(struct sway_container *parent,
		struct sway_container *child);

void container_insert_child(struct sway_container *parent,
		struct sway_container *child, int i);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void container_add_sibling(struct sway_container *parent,
		struct sway_container *child, bool after);

void container_detach(struct sway_container *child);

void container_replace(struct sway_container *container,
		struct sway_container *replacement);

void container_swap(struct sway_container *con1, struct sway_container *con2);

struct sway_container *container_split(struct sway_container *child,
		enum sway_container_layout layout);

bool container_is_transient_for(struct sway_container *child,
		struct sway_container *ancestor);

/**
 * Find any container that has the given mark and return it.
 */
struct sway_container *container_find_mark(char *mark);

/**
 * Find any container that has the given mark and remove the mark from the
 * container. Returns true if it matched a container.
 */
bool container_find_and_unmark(char *mark);

/**
 * Remove all marks from the container.
 */
void container_clear_marks(struct sway_container *container);

bool container_has_mark(struct sway_container *container, char *mark);

void container_add_mark(struct sway_container *container, char *mark);

void container_update_marks_textures(struct sway_container *container);

void container_raise_floating(struct sway_container *con);

bool container_is_scratchpad_hidden(struct sway_container *con);

bool container_is_scratchpad_hidden_or_child(struct sway_container *con);

#endif
