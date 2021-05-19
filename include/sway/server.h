#ifndef _SWAY_SERVER_H
#define _SWAY_SERVER_H
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "config.h"
#include "list.h"
#if HAVE_XWAYLAND
#include "sway/xwayland.h"
#endif

struct sway_transaction;

struct sway_server {
	struct wl_display *wl_display;
	struct wl_event_loop *wl_event_loop;
	const char *socket;

	struct wlr_backend *backend;
	struct wlr_backend *noop_backend;
	// secondary headless backend used for creating virtual outputs on-the-fly
	struct wlr_backend *headless_backend;

	struct wlr_compositor *compositor;
	struct wl_listener compositor_new_surface;

	struct wlr_data_device_manager *data_device_manager;

	struct sway_input_manager *input;

	struct wl_listener new_output;
	struct wl_listener output_layout_change;

	struct wlr_idle *idle;
	struct sway_idle_inhibit_manager_v1 *idle_inhibit_manager_v1;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener layer_shell_surface;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener xdg_shell_surface;

	struct wlr_tablet_manager_v2 *tablet_v2;

#if HAVE_XWAYLAND
	struct sway_xwayland xwayland;
	struct wl_listener xwayland_surface;
	struct wl_listener xwayland_ready;
#endif

	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

	struct wlr_server_decoration_manager *server_decoration_manager;
	struct wl_listener server_decoration;
	struct wl_list decorations; // sway_server_decoration::link

	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wl_listener xdg_decoration;
	struct wl_list xdg_decorations; // sway_xdg_decoration::link

	struct wlr_drm_lease_v1_manager *drm_lease_manager;
	struct wl_listener drm_lease_request;

	struct wlr_presentation *presentation;

	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wl_listener pointer_constraint;

	struct wlr_output_manager_v1 *output_manager_v1;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	struct wlr_output_power_manager_v1 *output_power_manager_v1;
	struct wl_listener output_power_manager_set_mode;
	struct wlr_input_method_manager_v2 *input_method;
	struct wlr_text_input_manager_v3 *text_input;
	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

	struct wlr_xdg_activation_v1 *xdg_activation_v1;
	struct wl_listener xdg_activation_v1_request_activate;

	// The timeout for transactions, after which a transaction is applied
	// regardless of readiness.
	size_t txn_timeout_ms;

	// Stores a transaction after it has been committed, but is waiting for
	// views to ack the new dimensions before being applied. A queued
	// transaction is frozen and must not have new instructions added to it.
	struct sway_transaction *queued_transaction;

	// Stores a pending transaction that will be committed once the existing
	// queued transaction is applied and freed. The pending transaction can be
	// updated with new instructions as needed.
	struct sway_transaction *pending_transaction;

	// Stores the nodes that have been marked as "dirty" and will be put into
	// the pending transaction.
	list_t *dirty_nodes;
};

extern struct sway_server server;

struct sway_debug {
	bool noatomic;         // Ignore atomic layout updates
	bool txn_timings;      // Log verbose messages about transactions
	bool txn_wait;         // Always wait for the timeout before applying

	enum {
		DAMAGE_DEFAULT,    // Default behaviour
		DAMAGE_HIGHLIGHT,  // Highlight regions of the screen being damaged
		DAMAGE_RERENDER,   // Render the full output when any damage occurs
	} damage;
};

extern struct sway_debug debug;

/* Prepares an unprivileged server_init by performing all privileged operations in advance */
bool server_privileged_prepare(struct sway_server *server);
bool server_init(struct sway_server *server);
void server_fini(struct sway_server *server);
bool server_start(struct sway_server *server);
void server_run(struct sway_server *server);

void handle_compositor_new_surface(struct wl_listener *listener, void *data);
void handle_new_output(struct wl_listener *listener, void *data);

void handle_idle_inhibitor_v1(struct wl_listener *listener, void *data);
void handle_layer_shell_surface(struct wl_listener *listener, void *data);
void handle_xdg_shell_surface(struct wl_listener *listener, void *data);
#if HAVE_XWAYLAND
void handle_xwayland_surface(struct wl_listener *listener, void *data);
#endif
void handle_server_decoration(struct wl_listener *listener, void *data);
void handle_xdg_decoration(struct wl_listener *listener, void *data);
void handle_pointer_constraint(struct wl_listener *listener, void *data);
void xdg_activation_v1_handle_request_activate(struct wl_listener *listener,
	void *data);

#endif
