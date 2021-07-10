#ifndef _SWAY_INPUT_SEAT_H
#define _SWAY_INPUT_SEAT_H

#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include "sway/config.h"
#include "sway/input/input-manager.h"
#include "sway/input/tablet.h"
#include "sway/input/text_input.h"

struct sway_seat;

struct sway_seatop_impl {
	void (*button)(struct sway_seat *seat, uint32_t time_msec,
			struct wlr_input_device *device, uint32_t button,
			enum wlr_button_state state);
	void (*pointer_motion)(struct sway_seat *seat, uint32_t time_msec);
	void (*pointer_axis)(struct sway_seat *seat,
			struct wlr_event_pointer_axis *event);
	void (*swipe_begin)(struct sway_seat *seat,
			struct wlr_event_pointer_swipe_begin *event);
	void (*swipe_update)(struct sway_seat *seat,
			struct wlr_event_pointer_swipe_update *event);
	void (*swipe_end)(struct sway_seat *seat,
			struct wlr_event_pointer_swipe_end *event);
	void (*rebase)(struct sway_seat *seat, uint32_t time_msec);
	void (*tablet_tool_motion)(struct sway_seat *seat,
			struct sway_tablet_tool *tool, uint32_t time_msec);
	void (*tablet_tool_tip)(struct sway_seat *seat, struct sway_tablet_tool *tool,
			uint32_t time_msec, enum wlr_tablet_tool_tip_state state);
	void (*end)(struct sway_seat *seat);
	void (*unref)(struct sway_seat *seat, struct sway_container *con);
	void (*render)(struct sway_seat *seat, struct sway_output *output,
			pixman_region32_t *damage);
	bool allow_set_cursor;
};

struct sway_seat_device {
	struct sway_seat *sway_seat;
	struct sway_input_device *input_device;
	struct sway_keyboard *keyboard;
	struct sway_switch *switch_device;
	struct sway_tablet *tablet;
	struct sway_tablet_pad *tablet_pad;
	struct wl_list link; // sway_seat::devices
};

struct sway_seat_node {
	struct sway_seat *seat;
	struct sway_node *node;

	struct wl_list link; // sway_seat::focus_stack

	struct wl_listener destroy;
};

struct sway_drag_icon {
	struct sway_seat *seat;
	struct wlr_drag_icon *wlr_drag_icon;
	struct wl_list link; // sway_root::drag_icons

	double x, y; // in layout-local coordinates

	struct wl_listener surface_commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
};

struct sway_drag {
	struct sway_seat *seat;
	struct wlr_drag *wlr_drag;
	struct wl_listener destroy;
};

struct sway_seat {
	struct wlr_seat *wlr_seat;
	struct sway_cursor *cursor;

	bool has_focus;
	struct wl_list focus_stack; // list of containers in focus order
	struct sway_workspace *workspace;
	char *prev_workspace_name; // for workspace back_and_forth

	// If the focused layer is set, views cannot receive keyboard focus
	struct wlr_layer_surface_v1 *focused_layer;

	// If exclusive_client is set, no other clients will receive input events
	struct wl_client *exclusive_client;

	// Last touch point
	int32_t touch_id;
	double touch_x, touch_y;

	// Seat operations (drag and resize)
	const struct sway_seatop_impl *seatop_impl;
	void *seatop_data;

	uint32_t last_button_serial;

	uint32_t idle_inhibit_sources, idle_wake_sources;

	list_t *deferred_bindings; // struct sway_binding

	struct sway_input_method_relay im_relay;

	struct wl_listener focus_destroy;
	struct wl_listener new_node;
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;

	struct wl_list devices; // sway_seat_device::link
	struct wl_list keyboard_groups; // sway_keyboard_group::link
	struct wl_list keyboard_shortcuts_inhibitors;
				// sway_keyboard_shortcuts_inhibitor::link

	struct wl_list link; // input_manager::seats
};

struct sway_pointer_constraint {
	struct sway_cursor *cursor;
	struct wlr_pointer_constraint_v1 *constraint;

	struct wl_listener set_region;
	struct wl_listener destroy;
};

struct sway_keyboard_shortcuts_inhibitor {
	struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;

	struct wl_listener destroy;

	struct wl_list link; // sway_seat::keyboard_shortcuts_inhibitors
};

struct sway_seat *seat_create(const char *seat_name);

void seat_destroy(struct sway_seat *seat);

void seat_add_device(struct sway_seat *seat,
		struct sway_input_device *device);

void seat_configure_device(struct sway_seat *seat,
		struct sway_input_device *device);

void seat_reset_device(struct sway_seat *seat,
		struct sway_input_device *input_device);

void seat_remove_device(struct sway_seat *seat,
		struct sway_input_device *device);

void seat_configure_xcursor(struct sway_seat *seat);

void seat_set_focus(struct sway_seat *seat, struct sway_node *node);

void seat_set_focus_container(struct sway_seat *seat,
		struct sway_container *con);

void seat_set_focus_workspace(struct sway_seat *seat,
		struct sway_workspace *ws);

/**
 * Manipulate the focus stack without triggering any other behaviour.
 *
 * This can be used to set focus_inactive by calling the function a second time
 * with the real focus.
 */
void seat_set_raw_focus(struct sway_seat *seat, struct sway_node *node);

void seat_set_focus_surface(struct sway_seat *seat,
		struct wlr_surface *surface, bool unfocus);

void seat_set_focus_layer(struct sway_seat *seat,
		struct wlr_layer_surface_v1 *layer);

void seat_set_exclusive_client(struct sway_seat *seat,
		struct wl_client *client);

struct sway_node *seat_get_focus(struct sway_seat *seat);

struct sway_workspace *seat_get_focused_workspace(struct sway_seat *seat);

// If a scratchpad container is fullscreen global, this can be used to try to
// determine the last focused workspace. Otherwise, this should yield the same
// results as seat_get_focused_workspace.
struct sway_workspace *seat_get_last_known_workspace(struct sway_seat *seat);

struct sway_container *seat_get_focused_container(struct sway_seat *seat);

/**
 * Return the last container to be focused for the seat (or the most recently
 * opened if no container has received focused) that is a child of the given
 * container. The focus-inactive container of the root window is the focused
 * container for the seat (if the seat does have focus). This function can be
 * used to determine what container gets focused next if the focused container
 * is destroyed, or focus moves to a container with children and we need to
 * descend into the next leaf in focus order.
 */
struct sway_node *seat_get_focus_inactive(struct sway_seat *seat,
		struct sway_node *node);

struct sway_container *seat_get_focus_inactive_tiling(struct sway_seat *seat,
		struct sway_workspace *workspace);

/**
 * Descend into the focus stack to find the focus-inactive view. Useful for
 * container placement when they change position in the tree.
 */
struct sway_container *seat_get_focus_inactive_view(struct sway_seat *seat,
		struct sway_node *ancestor);

/**
 * Return the immediate child of container which was most recently focused.
 */
struct sway_node *seat_get_active_tiling_child(struct sway_seat *seat,
		struct sway_node *parent);

/**
 * Iterate over the focus-inactive children of the container calling the
 * function on each.
 */
void seat_for_each_node(struct sway_seat *seat,
		void (*f)(struct sway_node *node, void *data), void *data);

void seat_apply_config(struct sway_seat *seat, struct seat_config *seat_config);

struct seat_config *seat_get_config(struct sway_seat *seat);

struct seat_config *seat_get_config_by_name(const char *name);

void seat_idle_notify_activity(struct sway_seat *seat,
		enum sway_input_idle_source source);

bool seat_is_input_allowed(struct sway_seat *seat, struct wlr_surface *surface);

void drag_icon_update_position(struct sway_drag_icon *icon);

enum wlr_edges find_resize_edge(struct sway_container *cont,
		struct wlr_surface *surface, struct sway_cursor *cursor);

void seatop_begin_default(struct sway_seat *seat);

void seatop_begin_down(struct sway_seat *seat, struct sway_container *con,
		uint32_t time_msec, int sx, int sy);

void seatop_begin_move_floating(struct sway_seat *seat,
		struct sway_container *con);

void seatop_begin_move_tiling_threshold(struct sway_seat *seat,
		struct sway_container *con);

void seatop_begin_move_tiling(struct sway_seat *seat,
		struct sway_container *con);

void seatop_begin_resize_floating(struct sway_seat *seat,
		struct sway_container *con, enum wlr_edges edge);

void seatop_begin_resize_tiling(struct sway_seat *seat,
		struct sway_container *con, enum wlr_edges edge);

struct sway_container *seat_get_focus_inactive_floating(struct sway_seat *seat,
		struct sway_workspace *workspace);

void seat_pointer_notify_button(struct sway_seat *seat, uint32_t time_msec,
		uint32_t button, enum wlr_button_state state);

void seat_consider_warp_to_focus(struct sway_seat *seat);

void seatop_button(struct sway_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state);

void seatop_pointer_motion(struct sway_seat *seat, uint32_t time_msec);

void seatop_pointer_axis(struct sway_seat *seat,
		struct wlr_event_pointer_axis *event);

void seatop_tablet_tool_tip(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state);

void seatop_tablet_tool_motion(struct sway_seat *seat,
		struct sway_tablet_tool *tool, uint32_t time_msec);

void seatop_swipe_begin(struct sway_seat *seat,
		struct wlr_event_pointer_swipe_begin *event);
void seatop_swipe_update(struct sway_seat *seat,
		struct wlr_event_pointer_swipe_update *event);
void seatop_swipe_end(struct sway_seat *seat,
		struct wlr_event_pointer_swipe_end *event);

void seatop_rebase(struct sway_seat *seat, uint32_t time_msec);

/**
 * End a seatop (ie. free any seatop specific resources).
 */
void seatop_end(struct sway_seat *seat);

/**
 * Instructs the seatop implementation to drop any references to the given
 * container (eg. because the container is destroying).
 * The seatop may choose to abort itself in response to this.
 */
void seatop_unref(struct sway_seat *seat, struct sway_container *con);

/**
 * Instructs a seatop to render anything that it needs to render
 * (eg. dropzone for move-tiling)
 */
void seatop_render(struct sway_seat *seat, struct sway_output *output,
		pixman_region32_t *damage);

bool seatop_allows_set_cursor(struct sway_seat *seat);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the given surface
 * or NULL if none exists.
 */
struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(const struct sway_seat *seat,
		const struct wlr_surface *surface);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the currently
 * focused surface of a seat or NULL if none exists.
 */
struct sway_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(const struct sway_seat *seat);

#endif
