#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include "sway/config.h"
#include "sway/desktop.h"
#include "sway/desktop/idle_inhibit_v1.h"
#include "sway/desktop/transaction.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/output.h"
#include "sway/tree/container.h"
#include "sway/tree/node.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "list.h"
#include "log.h"

struct sway_transaction {
	struct wl_event_source *timer;
	list_t *instructions;   // struct sway_transaction_instruction *
	size_t num_waiting;
	size_t num_configures;
	struct timespec commit_time;
};

struct sway_transaction_instruction {
	struct sway_transaction *transaction;
	struct sway_node *node;
	union {
		struct sway_output_state output_state;
		struct sway_workspace_state workspace_state;
		struct sway_container_state container_state;
	};
	uint32_t serial;
	bool server_request;
	bool waiting;
};

static struct sway_transaction *transaction_create(void) {
	struct sway_transaction *transaction =
		calloc(1, sizeof(struct sway_transaction));
	if (!sway_assert(transaction, "Unable to allocate transaction")) {
		return NULL;
	}
	transaction->instructions = create_list();
	return transaction;
}

static void transaction_destroy(struct sway_transaction *transaction) {
	// Free instructions
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;
		node->ntxnrefs--;
		if (node->instruction == instruction) {
			node->instruction = NULL;
		}
		if (node->destroying && node->ntxnrefs == 0) {
			switch (node->type) {
			case N_ROOT:
				sway_assert(false, "Never reached");
				break;
			case N_OUTPUT:
				output_destroy(node->sway_output);
				break;
			case N_WORKSPACE:
				workspace_destroy(node->sway_workspace);
				break;
			case N_CONTAINER:
				container_destroy(node->sway_container);
				break;
			}
		}
		free(instruction);
	}
	list_free(transaction->instructions);

	if (transaction->timer) {
		wl_event_source_remove(transaction->timer);
	}
	free(transaction);
}

static void copy_output_state(struct sway_output *output,
		struct sway_transaction_instruction *instruction) {
	struct sway_output_state *state = &instruction->output_state;
	if (state->workspaces) {
		state->workspaces->length = 0;
	} else {
		state->workspaces = create_list();
	}
	list_cat(state->workspaces, output->workspaces);

	state->active_workspace = output_get_active_workspace(output);
}

static void copy_workspace_state(struct sway_workspace *ws,
		struct sway_transaction_instruction *instruction) {
	struct sway_workspace_state *state = &instruction->workspace_state;

	state->fullscreen = ws->fullscreen;
	state->x = ws->x;
	state->y = ws->y;
	state->width = ws->width;
	state->height = ws->height;
	state->layout = ws->layout;

	state->output = ws->output;
	if (state->floating) {
		state->floating->length = 0;
	} else {
		state->floating = create_list();
	}
	if (state->tiling) {
		state->tiling->length = 0;
	} else {
		state->tiling = create_list();
	}
	list_cat(state->floating, ws->floating);
	list_cat(state->tiling, ws->tiling);

	struct sway_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &ws->node;

	// Set focused_inactive_child to the direct tiling child
	struct sway_container *focus = seat_get_focus_inactive_tiling(seat, ws);
	if (focus) {
		while (focus->pending.parent) {
			focus = focus->pending.parent;
		}
	}
	state->focused_inactive_child = focus;
}

static void copy_container_state(struct sway_container *container,
		struct sway_transaction_instruction *instruction) {
	struct sway_container_state *state = &instruction->container_state;

	if (state->children) {
		list_free(state->children);
	}

	memcpy(state, &container->pending, sizeof(struct sway_container_state));

	if (!container->view) {
		// We store a copy of the child list to avoid having it mutated after
		// we copy the state.
		state->children = create_list();
		list_cat(state->children, container->pending.children);
	} else {
		state->children = NULL;
	}

	struct sway_seat *seat = input_manager_current_seat();
	state->focused = seat_get_focus(seat) == &container->node;

	if (!container->view) {
		struct sway_node *focus =
			seat_get_active_tiling_child(seat, &container->node);
		state->focused_inactive_child = focus ? focus->sway_container : NULL;
	}
}

static void transaction_add_node(struct sway_transaction *transaction,
		struct sway_node *node, bool server_request) {
	struct sway_transaction_instruction *instruction = NULL;

	// Check if we have an instruction for this node already, in which case we
	// update that instead of creating a new one.
	if (node->ntxnrefs > 0) {
		for (int idx = 0; idx < transaction->instructions->length; idx++) {
			struct sway_transaction_instruction *other =
				transaction->instructions->items[idx];
			if (other->node == node) {
				instruction = other;
				break;
			}
		}
	}

	if (!instruction) {
		instruction = calloc(1, sizeof(struct sway_transaction_instruction));
		if (!sway_assert(instruction, "Unable to allocate instruction")) {
			return;
		}
		instruction->transaction = transaction;
		instruction->node = node;
		instruction->server_request = server_request;

		list_add(transaction->instructions, instruction);
		node->ntxnrefs++;
	} else if (server_request) {
		instruction->server_request = true;
	}

	switch (node->type) {
	case N_ROOT:
		break;
	case N_OUTPUT:
		copy_output_state(node->sway_output, instruction);
		break;
	case N_WORKSPACE:
		copy_workspace_state(node->sway_workspace, instruction);
		break;
	case N_CONTAINER:
		copy_container_state(node->sway_container, instruction);
		break;
	}
}

static void apply_output_state(struct sway_output *output,
		struct sway_output_state *state) {
	output_damage_whole(output);
	list_free(output->current.workspaces);
	memcpy(&output->current, state, sizeof(struct sway_output_state));
	output_damage_whole(output);
}

static void apply_workspace_state(struct sway_workspace *ws,
		struct sway_workspace_state *state) {
	output_damage_whole(ws->current.output);
	list_free(ws->current.floating);
	list_free(ws->current.tiling);
	memcpy(&ws->current, state, sizeof(struct sway_workspace_state));
	output_damage_whole(ws->current.output);
}

static void apply_container_state(struct sway_container *container,
		struct sway_container_state *state) {
	struct sway_view *view = container->view;

	// There are separate children lists for each instruction state, the
	// container's current state and the container's pending state
	// (ie. con->children). The list itself needs to be freed here.
	// Any child containers which are being deleted will be cleaned up in
	// transaction_destroy().
	list_free(container->current.children);

	memcpy(&container->current, state, sizeof(struct sway_container_state));

	// If the view hasn't responded to the configure, center it within
	// the container. This is important for fullscreen views which
	// refuse to resize to the size of the output.
	if (view && view->surface) {
		view_center_surface(view);
	}

	// Damage the new location
	desktop_damage_whole_container(container);
	if (view && view->surface) {
		struct wlr_surface *surface = view->surface;
		struct wlr_box box = {
			.x = container->current.content_x - view->geometry.x,
			.y = container->current.content_y - view->geometry.y,
			.width = surface->current.width,
			.height = surface->current.height,
		};
		desktop_damage_box(&box);
	}

	if (!container->node.destroying) {
		container_discover_outputs(container);
	}
}

/**
 * Apply a transaction to the "current" state of the tree.
 */
static void transaction_apply(struct sway_transaction *transaction) {
	sway_log(SWAY_DEBUG, "Applying transaction %p", transaction);
	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *commit = &transaction->commit_time;
		float ms = (now.tv_sec - commit->tv_sec) * 1000 +
			(now.tv_nsec - commit->tv_nsec) / 1000000.0;
		sway_log(SWAY_DEBUG, "Transaction %p: %.1fms waiting "
				"(%.1f frames if 60Hz)", transaction, ms, ms / (1000.0f / 60));
	}

	// Apply the instruction state to the node's current state
	// Applying the last instruction may destroy the transaction
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;

		switch (node->type) {
		case N_ROOT:
			break;
		case N_OUTPUT:
			apply_output_state(node->sway_output, &instruction->output_state);
			break;
		case N_WORKSPACE:
			apply_workspace_state(node->sway_workspace,
					&instruction->workspace_state);
			break;
		case N_CONTAINER:
			apply_container_state(node->sway_container,
					&instruction->container_state);
			break;
		}

		node->instruction = NULL;
	}

	cursor_rebase_all();
}

static void transaction_commit_pending(void);

static void transaction_progress(void) {
	if (!server.queued_transaction) {
		return;
	}
	if (server.queued_transaction->num_waiting > 0) {
		return;
	}
	transaction_apply(server.queued_transaction);
	transaction_destroy(server.queued_transaction);
	server.queued_transaction = NULL;

	if (!server.pending_transaction) {
		sway_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
		return;
	}

	transaction_commit_pending();
}

static int handle_timeout(void *data) {
	struct sway_transaction *transaction = data;
	sway_log(SWAY_DEBUG, "Transaction %p timed out (%zi waiting)",
			transaction, transaction->num_waiting);

	struct sway_view **views =
		calloc(transaction->instructions->length, sizeof(struct sway_view *));
	size_t views_len = 0;

	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;

		if (node->type == N_CONTAINER && node->sway_container->view &&
				node->sway_container->view->surface_locked) {
			views[views_len] = node->sway_container->view;
			views_len++;
		}
	}

	// This will destroy the transaction
	for (size_t i = 0; i < views_len; ++i) {
		view_unlock_pending(views[i]);
	}
	free(views);

	return 0;
}

static bool should_configure(struct sway_node *node,
		struct sway_transaction_instruction *instruction) {
	if (!node_is_view(node)) {
		return false;
	}
	if (node->destroying) {
		return false;
	}
	if (!instruction->server_request) {
		return false;
	}
	struct sway_container_state *cstate = &node->sway_container->current;
	struct sway_container_state *istate = &instruction->container_state;
#if HAVE_XWAYLAND
	// Xwayland views are position-aware and need to be reconfigured
	// when their position changes.
	if (node->sway_container->view->type == SWAY_VIEW_XWAYLAND) {
		// Sway logical coordinates are doubles, but they get truncated to
		// integers when sent to Xwayland through `xcb_configure_window`.
		// X11 apps will not respond to duplicate configure requests (from their
		// truncated point of view) and cause transactions to time out.
		if ((int)cstate->content_x != (int)istate->content_x ||
				(int)cstate->content_y != (int)istate->content_y) {
			return true;
		}
	}
#endif
	if (cstate->content_width == istate->content_width &&
			cstate->content_height == istate->content_height) {
		return false;
	}
	return true;
}

static void transaction_commit(struct sway_transaction *transaction) {
	sway_log(SWAY_DEBUG, "Transaction %p committing with %i instructions",
			transaction, transaction->instructions->length);
	transaction->num_waiting = 0;
	for (int i = 0; i < transaction->instructions->length; ++i) {
		struct sway_transaction_instruction *instruction =
			transaction->instructions->items[i];
		struct sway_node *node = instruction->node;
		bool hidden = node_is_view(node) &&
			!view_is_visible(node->sway_container->view);
		if (should_configure(node, instruction)) {
			instruction->serial = view_configure(node->sway_container->view,
					instruction->container_state.content_x,
					instruction->container_state.content_y,
					instruction->container_state.content_width,
					instruction->container_state.content_height);
			if (!hidden) {
				instruction->waiting = true;
				++transaction->num_waiting;
			}

			// From here on we are freezing surface commits for the view, which
			// means we can send a frame done event to make the client redraw it
			// as soon as possible. Additionally, this is required if a view is
			// mapping and its default geometry doesn't intersect an output.
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			wlr_surface_send_frame_done(
					node->sway_container->view->surface, &now);
		}
		if (!hidden && node_is_view(node) &&
				!node->sway_container->view->surface_locked) {
			view_lock_pending(node->sway_container->view);
			memcpy(&node->sway_container->view->saved_geometry,
					&node->sway_container->view->geometry,
					sizeof(struct wlr_box));
		}
		node->instruction = instruction;
	}
	transaction->num_configures = transaction->num_waiting;
	if (debug.txn_timings) {
		clock_gettime(CLOCK_MONOTONIC, &transaction->commit_time);
	}
	if (debug.noatomic) {
		transaction->num_waiting = 0;
	} else if (debug.txn_wait) {
		// Force the transaction to time out even if all views are ready.
		// We do this by inflating the waiting counter.
		transaction->num_waiting += 1000000;
	}

	if (transaction->num_waiting) {
		// Set up a timer which the views must respond within
		transaction->timer = wl_event_loop_add_timer(server.wl_event_loop,
				handle_timeout, transaction);
		if (transaction->timer) {
			wl_event_source_timer_update(transaction->timer,
					server.txn_timeout_ms);
		} else {
			sway_log_errno(SWAY_ERROR, "Unable to create transaction timer "
					"(some imperfect frames might be rendered)");
			transaction->num_waiting = 0;
		}
	}
}

static void transaction_commit_pending(void) {
	if (server.queued_transaction) {
		return;
	}
	struct sway_transaction *transaction = server.pending_transaction;
	server.pending_transaction = NULL;
	server.queued_transaction = transaction;
	transaction_commit(transaction);
	transaction_progress();
}

static void set_instruction_ready(
		struct sway_transaction_instruction *instruction) {
	struct sway_transaction *transaction = instruction->transaction;

	if (debug.txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *start = &transaction->commit_time;
		float ms = (now.tv_sec - start->tv_sec) * 1000 +
			(now.tv_nsec - start->tv_nsec) / 1000000.0;
		sway_log(SWAY_DEBUG, "Transaction %p: %zi/%zi ready in %.1fms (%s)",
				transaction,
				transaction->num_configures - transaction->num_waiting + 1,
				transaction->num_configures, ms,
				instruction->node->sway_container->title);
	}

	// If the transaction has timed out then its num_waiting will be 0 already.
	if (instruction->waiting && transaction->num_waiting > 0 &&
			--transaction->num_waiting == 0) {
		sway_log(SWAY_DEBUG, "Transaction %p is ready", transaction);
		wl_event_source_timer_update(transaction->timer, 0);
	}

	instruction->node->instruction = NULL;
	transaction_progress();
}

void transaction_notify_view_ready_by_serial(struct sway_view *view,
		uint32_t serial) {
	struct sway_transaction_instruction *instruction =
		view->container->node.instruction;
	if (instruction != NULL && instruction->serial == serial) {
		set_instruction_ready(instruction);
	}
}

void transaction_unlock_view_by_serial(struct sway_view *view,
		uint32_t serial) {
	struct sway_transaction_instruction *instruction =
		view->container->node.instruction;
	if (instruction != NULL && instruction->serial == serial) {
		view_unlock_pending(view);
	}
}

void transaction_notify_view_ready_by_geometry(struct sway_view *view,
		double x, double y, int width, int height) {
	struct sway_transaction_instruction *instruction =
		view->container->node.instruction;
	if (instruction != NULL &&
			(int)instruction->container_state.content_x == (int)x &&
			(int)instruction->container_state.content_y == (int)y &&
			instruction->container_state.content_width == width &&
			instruction->container_state.content_height == height) {
		set_instruction_ready(instruction);
	}
}

static void _transaction_commit_dirty(bool server_request) {
	if (!server.dirty_nodes->length) {
		return;
	}

	if (!server.pending_transaction) {
		server.pending_transaction = transaction_create();
		if (!server.pending_transaction) {
			return;
		}
	}

	for (int i = 0; i < server.dirty_nodes->length; ++i) {
		struct sway_node *node = server.dirty_nodes->items[i];
		transaction_add_node(server.pending_transaction, node, server_request);
		node->dirty = false;
	}
	server.dirty_nodes->length = 0;

	transaction_commit_pending();
}

void transaction_commit_dirty(void) {
	_transaction_commit_dirty(true);
}

void transaction_commit_dirty_client(void) {
	_transaction_commit_dirty(false);
}
