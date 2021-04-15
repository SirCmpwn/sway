#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/commands.h"
#include "sway/input/cursor.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "stringop.h"
#include "list.h"
#include "log.h"
#include "util.h"

static const char expected_syntax[] =
	"Expected 'move <left|right|up|down> <[px] px>' or "
	"'move [--no-auto-back-and-forth] <container|window> [to] workspace <name>' or "
	"'move <container|window|workspace> [to] output <name|direction>' or "
	"'move <container|window> [to] mark <mark>'";

static struct sway_output *output_in_direction(const char *direction_string,
		struct sway_output *reference, int ref_lx, int ref_ly) {
	if (strcasecmp(direction_string, "current") == 0) {
		struct sway_workspace *active_ws =
			seat_get_focused_workspace(config->handler_context.seat);
		if (!active_ws) {
			return NULL;
		}
		return active_ws->output;
	}

	struct {
		char *name;
		enum wlr_direction direction;
	} names[] = {
		{ "up", WLR_DIRECTION_UP },
		{ "down", WLR_DIRECTION_DOWN },
		{ "left", WLR_DIRECTION_LEFT },
		{ "right", WLR_DIRECTION_RIGHT },
	};

	enum wlr_direction direction = 0;

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (strcasecmp(names[i].name, direction_string) == 0) {
			direction = names[i].direction;
			break;
		}
	}

	if (reference && direction) {
		struct wlr_output *target = wlr_output_layout_adjacent_output(
				root->output_layout, direction, reference->wlr_output,
				ref_lx, ref_ly);

		if (!target) {
			target = wlr_output_layout_farthest_output(
					root->output_layout, opposite_direction(direction),
					reference->wlr_output, ref_lx, ref_ly);
		}

		if (target) {
			return target->data;
		}
	}

	return output_by_name_or_id(direction_string);
}

static bool is_parallel(enum sway_container_layout layout,
		enum wlr_direction dir) {
	switch (layout) {
	case L_TABBED:
	case L_HORIZ:
		return dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_RIGHT;
	case L_STACKED:
	case L_VERT:
		return dir == WLR_DIRECTION_UP || dir == WLR_DIRECTION_DOWN;
	default:
		return false;
	}
}

/**
 * Ensures all seats focus the fullscreen container if needed.
 */
static void workspace_focus_fullscreen(struct sway_workspace *workspace) {
	if (!workspace->fullscreen) {
		return;
	}
	struct sway_seat *seat;
	struct sway_workspace *focus_ws;
	wl_list_for_each(seat, &server.input->seats, link) {
		focus_ws = seat_get_focused_workspace(seat);
		if (focus_ws == workspace) {
			struct sway_node *new_focus =
				seat_get_focus_inactive(seat, &workspace->fullscreen->node);
			seat_set_raw_focus(seat, new_focus);
		}
	}
}

static void container_move_to_container_from_direction(
		struct sway_container *container, struct sway_container *destination,
		enum wlr_direction move_dir) {
	if (destination->view) {
		if (destination->pending.parent == container->pending.parent &&
				destination->pending.workspace == container->pending.workspace) {
			sway_log(SWAY_DEBUG, "Swapping siblings");
			list_t *siblings = container_get_siblings(container);
			int container_index = list_find(siblings, container);
			int destination_index = list_find(siblings, destination);
			list_swap(siblings, container_index, destination_index);
			container_update_representation(container);
		} else {
			sway_log(SWAY_DEBUG, "Promoting to sibling of cousin");
			int offset =
				move_dir == WLR_DIRECTION_LEFT || move_dir == WLR_DIRECTION_UP;
			int index = container_sibling_index(destination) + offset;
			if (destination->pending.parent) {
				container_insert_child(destination->pending.parent, container, index);
			} else {
				workspace_insert_tiling(destination->pending.workspace,
						container, index);
			}
			container->pending.width = container->pending.height = 0;
			container->width_fraction = container->height_fraction = 0;
			workspace_squash(destination->pending.workspace);
		}
		return;
	}

	if (is_parallel(destination->pending.layout, move_dir)) {
		sway_log(SWAY_DEBUG, "Reparenting container (parallel)");
		int index =
			move_dir == WLR_DIRECTION_RIGHT || move_dir == WLR_DIRECTION_DOWN ?
			0 : destination->pending.children->length;
		container_insert_child(destination, container, index);
		container->pending.width = container->pending.height = 0;
		container->width_fraction = container->height_fraction = 0;
		workspace_squash(destination->pending.workspace);
		return;
	}

	sway_log(SWAY_DEBUG, "Reparenting container (perpendicular)");
	struct sway_node *focus_inactive = seat_get_active_tiling_child(
			config->handler_context.seat, &destination->node);
	if (!focus_inactive || focus_inactive == &destination->node) {
		// The container has no children
		container_add_child(destination, container);
		return;
	}

	// Try again but with the child
	container_move_to_container_from_direction(container,
			focus_inactive->sway_container, move_dir);
}

static void container_move_to_workspace_from_direction(
		struct sway_container *container, struct sway_workspace *workspace,
		enum wlr_direction move_dir) {
	container->pending.width = container->pending.height = 0;
	container->width_fraction = container->height_fraction = 0;

	if (is_parallel(workspace->layout, move_dir)) {
		sway_log(SWAY_DEBUG, "Reparenting container (parallel)");
		int index =
			move_dir == WLR_DIRECTION_RIGHT || move_dir == WLR_DIRECTION_DOWN ?
			0 : workspace->tiling->length;
		workspace_insert_tiling(workspace, container, index);
		return;
	}

	sway_log(SWAY_DEBUG, "Reparenting container (perpendicular)");
	struct sway_container *focus_inactive = seat_get_focus_inactive_tiling(
			config->handler_context.seat, workspace);
	if (!focus_inactive) {
		// The workspace has no tiling children
		workspace_add_tiling(workspace, container);
		return;
	}
	while (focus_inactive->pending.parent) {
		focus_inactive = focus_inactive->pending.parent;
	}
	container_move_to_container_from_direction(container, focus_inactive,
			move_dir);
}

static void container_move_to_workspace(struct sway_container *container,
		struct sway_workspace *workspace) {
	if (container->pending.workspace == workspace) {
		return;
	}
	struct sway_workspace *old_workspace = container->pending.workspace;
	if (container_is_floating(container)) {
		struct sway_output *old_output = container->pending.workspace->output;
		container_detach(container);
		workspace_add_floating(workspace, container);
		container_handle_fullscreen_reparent(container);
		// If changing output, center it within the workspace
		if (old_output != workspace->output && !container->pending.fullscreen_mode) {
			container_floating_move_to_center(container);
		}
	} else {
		container_detach(container);
		if (workspace_is_empty(workspace) && container->pending.children) {
			workspace_unwrap_children(workspace, container);
		} else {
			container->pending.width = container->pending.height = 0;
			container->width_fraction = container->height_fraction = 0;
			workspace_add_tiling(workspace, container);
		}
		container_update_representation(container);
	}
	if (container->view) {
		ipc_event_window(container, "move");
	}
	workspace_detect_urgent(old_workspace);
	workspace_detect_urgent(workspace);
	workspace_focus_fullscreen(workspace);
}

static void container_move_to_container(struct sway_container *container,
		struct sway_container *destination) {
	if (container == destination
			|| container_has_ancestor(container, destination)
			|| container_has_ancestor(destination, container)) {
		return;
	}
	if (container_is_floating(container)) {
		container_move_to_workspace(container, destination->pending.workspace);
		return;
	}
	struct sway_workspace *old_workspace = container->pending.workspace;

	container_detach(container);
	container->pending.width = container->pending.height = 0;
	container->width_fraction = container->height_fraction = 0;

	if (destination->view) {
		container_add_sibling(destination, container, 1);
	} else {
		container_add_child(destination, container);
	}

	if (container->view) {
		ipc_event_window(container, "move");
	}

	if (destination->pending.workspace) {
		workspace_focus_fullscreen(destination->pending.workspace);
		workspace_detect_urgent(destination->pending.workspace);
	}

	if (old_workspace && old_workspace != destination->pending.workspace) {
		workspace_detect_urgent(old_workspace);
	}
}

static bool container_move_to_next_output(struct sway_container *container,
		struct sway_output *output, enum wlr_direction move_dir) {
	struct sway_output *next_output =
		output_get_in_direction(output, move_dir);
	if (next_output) {
		struct sway_workspace *ws = output_get_active_workspace(next_output);
		if (!sway_assert(ws, "Expected output to have a workspace")) {
			return false;
		}
		switch (container->pending.fullscreen_mode) {
		case FULLSCREEN_NONE:
			container_move_to_workspace_from_direction(container, ws, move_dir);
			return true;
		case FULLSCREEN_WORKSPACE:
			container_move_to_workspace(container, ws);
			return true;
		case FULLSCREEN_GLOBAL:
			return false;
		}
	}
	return false;
}

// Returns true if moved
static bool container_move_in_direction(struct sway_container *container,
		enum wlr_direction move_dir) {
	// If moving a fullscreen view, only consider outputs
	switch (container->pending.fullscreen_mode) {
	case FULLSCREEN_NONE:
		break;
	case FULLSCREEN_WORKSPACE:
		return container_move_to_next_output(container,
				container->pending.workspace->output, move_dir);
	case FULLSCREEN_GLOBAL:
		return false;
	}

	int offs =
		move_dir == WLR_DIRECTION_LEFT || move_dir == WLR_DIRECTION_UP ? -1 : 1;
	int index = -1;
	int	desired = -1;
	list_t *siblings = NULL;
	struct sway_container *target = NULL;

	// Look for a suitable ancestor of the container to move within
	struct sway_container *ancestor = NULL;
	struct sway_container *current = container;
	bool wrapped = false;
	while (!ancestor) {
		// Don't allow containers to move out of their
		// fullscreen or floating parent
		if (current->pending.fullscreen_mode || container_is_floating(current)) {
			return false;
		}

		enum sway_container_layout parent_layout = container_parent_layout(current);
		if (!is_parallel(parent_layout, move_dir)) {
			if (!current->pending.parent) {
				// No parallel parent, so we reorient the workspace
				current = workspace_wrap_children(current->pending.workspace);
				current->pending.workspace->layout =
					move_dir == WLR_DIRECTION_LEFT ||
					move_dir == WLR_DIRECTION_RIGHT ?
					L_HORIZ : L_VERT;
				container->pending.height = container->pending.width = 0;
				container->height_fraction = container->width_fraction = 0;
				workspace_update_representation(current->pending.workspace);
				wrapped = true;
			} else {
				// Keep looking for a parallel parent
				current = current->pending.parent;
			}
			continue;
		}

		// Only scratchpad hidden containers don't have siblings
		// so siblings != NULL here
		siblings = container_get_siblings(current);
		index = list_find(siblings, current);
		desired = index + offs;
		target = desired == -1 || desired == siblings->length ?
				NULL : siblings->items[desired];

		// If the move is simple we can complete it here early
		if (current == container) {
			if (target) {
				// Container will swap with or descend into its neighbor
				container_move_to_container_from_direction(container,
						target, move_dir);
				return true;
			} else if (!container->pending.parent) {
				// Container is at workspace level so we move it to the
				// next workspace if possible
				return container_move_to_next_output(container,
						current->pending.workspace->output, move_dir);
			} else {
				// Container has escaped its immediate parallel parent
				current = current->pending.parent;
				continue;
			}
		}

		// We found a suitable ancestor, the loop will end
		ancestor = current;
	}

	if (target) {
		// Container will move in with its cousin
		container_move_to_container_from_direction(container,
				target, move_dir);
		return true;
	} else if (!wrapped && !container->pending.parent->pending.parent &&
			container->pending.parent->pending.children->length == 1) {
		// Treat singleton children as if they are at workspace level like i3
		// https://github.com/i3/i3/blob/1d9160f2d247dbaa83fb62f02fd7041dec767fc2/src/move.c#L367
		return container_move_to_next_output(container,
				ancestor->pending.workspace->output, move_dir);
	} else {
		// Container will be promoted
		struct sway_container *old_parent = container->pending.parent;
		if (ancestor->pending.parent) {
			// Container will move in with its parent
			container_insert_child(ancestor->pending.parent, container,
					index + (offs < 0 ? 0 : 1));
		} else {
			// Container will move to workspace level,
			// may be re-split by workspace_layout
			workspace_insert_tiling(ancestor->pending.workspace, container,
					index + (offs < 0 ? 0 : 1));
		}
		ancestor->pending.height = ancestor->pending.width = 0;
		ancestor->height_fraction = ancestor->width_fraction = 0;
		if (old_parent) {
			container_reap_empty(old_parent);
		}
		workspace_squash(container->pending.workspace);
		return true;
	}
}

static struct cmd_results *cmd_move_to_scratchpad(void);

static struct cmd_results *cmd_move_container(bool no_auto_back_and_forth,
		int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move container/window",
				EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	struct sway_node *node = config->handler_context.node;
	struct sway_workspace *workspace = config->handler_context.workspace;
	struct sway_container *container = config->handler_context.container;
	if (node->type == N_WORKSPACE) {
		if (workspace->tiling->length == 0) {
			return cmd_results_new(CMD_FAILURE,
					"Can't move an empty workspace");
		}
		container = workspace_wrap_children(workspace);
	}

	if (container->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		return cmd_results_new(CMD_FAILURE,
				"Can't move fullscreen global container");
	}

	struct sway_seat *seat = config->handler_context.seat;
	struct sway_container *old_parent = container->pending.parent;
	struct sway_workspace *old_ws = container->pending.workspace;
	struct sway_output *old_output = old_ws ? old_ws->output : NULL;
	struct sway_node *destination = NULL;

	// determine destination
	if (strcasecmp(argv[0], "workspace") == 0) {
		// move container to workspace x
		struct sway_workspace *ws = NULL;
		char *ws_name = NULL;
		if (strcasecmp(argv[1], "next") == 0 ||
				strcasecmp(argv[1], "prev") == 0 ||
				strcasecmp(argv[1], "next_on_output") == 0 ||
				strcasecmp(argv[1], "prev_on_output") == 0 ||
				strcasecmp(argv[1], "current") == 0) {
			ws = workspace_by_name(argv[1]);
		} else if (strcasecmp(argv[1], "back_and_forth") == 0) {
			if (!(ws = workspace_by_name(argv[1]))) {
				if (seat->prev_workspace_name) {
					ws_name = strdup(seat->prev_workspace_name);
				} else {
					return cmd_results_new(CMD_FAILURE,
							"No workspace was previously active.");
				}
			}
		} else {
			if (strcasecmp(argv[1], "number") == 0) {
				// move [window|container] [to] "workspace number x"
				if (argc < 3) {
					return cmd_results_new(CMD_INVALID, expected_syntax);
				}
				if (!isdigit(argv[2][0])) {
					return cmd_results_new(CMD_INVALID,
							"Invalid workspace number '%s'", argv[2]);
				}
				ws_name = join_args(argv + 2, argc - 2);
				ws = workspace_by_number(ws_name);
			} else {
				ws_name = join_args(argv + 1, argc - 1);
				ws = workspace_by_name(ws_name);
			}

			if (!no_auto_back_and_forth && config->auto_back_and_forth &&
					seat->prev_workspace_name) {
				// auto back and forth move
				if (old_ws && old_ws->name &&
						strcmp(old_ws->name, ws_name) == 0) {
					// if target workspace is the current one
					free(ws_name);
					ws_name = strdup(seat->prev_workspace_name);
					ws = workspace_by_name(ws_name);
				}
			}
		}
		if (!ws) {
			// We have to create the workspace, but if the container is
			// sticky and the workspace is going to be created on the same
			// output, we'll bail out first.
			if (container_is_sticky_or_child(container)) {
				struct sway_output *new_output =
					workspace_get_initial_output(ws_name);
				if (old_output == new_output) {
					free(ws_name);
					return cmd_results_new(CMD_FAILURE,
							"Can't move sticky container to another workspace "
							"on the same output");
				}
			}
			ws = workspace_create(NULL, ws_name);
		}
		free(ws_name);
		struct sway_container *dst = seat_get_focus_inactive_tiling(seat, ws);
		destination = dst ? &dst->node : &ws->node;
	} else if (strcasecmp(argv[0], "output") == 0) {
		struct sway_output *new_output = output_in_direction(argv[1],
				old_output, container->pending.x, container->pending.y);
		if (!new_output) {
			return cmd_results_new(CMD_FAILURE,
				"Can't find output with name/direction '%s'", argv[1]);
		}
		destination = seat_get_focus_inactive(seat, &new_output->node);
	} else if (strcasecmp(argv[0], "mark") == 0) {
		struct sway_container *dest_con = container_find_mark(argv[1]);
		if (dest_con == NULL) {
			return cmd_results_new(CMD_FAILURE,
					"Mark '%s' not found", argv[1]);
		}
		destination = &dest_con->node;
	} else {
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	if (destination->type == N_CONTAINER &&
			container_is_scratchpad_hidden(destination->sway_container)) {
		return cmd_move_to_scratchpad();
	}

	if (container_is_sticky_or_child(container) && old_output &&
			node_has_ancestor(destination, &old_output->node)) {
		return cmd_results_new(CMD_FAILURE, "Can't move sticky "
				"container to another workspace on the same output");
	}

	struct sway_output *new_output = node_get_output(destination);
	struct sway_workspace *new_output_last_ws = NULL;
	if (new_output && old_output != new_output) {
		new_output_last_ws = output_get_active_workspace(new_output);
	}

	// save focus, in case it needs to be restored
	struct sway_node *focus = seat_get_focus(seat);

	// move container
	if (container_is_scratchpad_hidden_or_child(container)) {
		container_detach(container);
		root_scratchpad_show(container);
	}
	switch (destination->type) {
	case N_WORKSPACE:
		container_move_to_workspace(container, destination->sway_workspace);
		break;
	case N_OUTPUT: {
			struct sway_output *output = destination->sway_output;
			struct sway_workspace *ws = output_get_active_workspace(output);
			if (!sway_assert(ws, "Expected output to have a workspace")) {
				return cmd_results_new(CMD_FAILURE,
						"Expected output to have a workspace");
			}
			container_move_to_workspace(container, ws);
		}
		break;
	case N_CONTAINER:
		container_move_to_container(container, destination->sway_container);
		break;
	case N_ROOT:
		break;
	}

	// restore focus on destination output back to its last active workspace
	struct sway_workspace *new_workspace = new_output ?
		output_get_active_workspace(new_output) : NULL;
	if (new_output &&
			!sway_assert(new_workspace, "Expected output to have a workspace")) {
		return cmd_results_new(CMD_FAILURE,
				"Expected output to have a workspace");
	}

	if (new_output_last_ws && new_output_last_ws != new_workspace) {
		struct sway_node *new_output_last_focus =
			seat_get_focus_inactive(seat, &new_output_last_ws->node);
		seat_set_raw_focus(seat, new_output_last_focus);
	}

	// restore focus
	if (focus == &container->node) {
		focus = NULL;
		if (old_parent) {
			focus = seat_get_focus_inactive(seat, &old_parent->node);
		}
		if (!focus && old_ws) {
			focus = seat_get_focus_inactive(seat, &old_ws->node);
		}
	}
	seat_set_focus(seat, focus);

	// clean-up, destroying parents if the container was the last child
	if (old_parent) {
		container_reap_empty(old_parent);
	} else if (old_ws) {
		workspace_consider_destroy(old_ws);
	}

	// arrange windows
	if (root->fullscreen_global) {
		arrange_root();
	} else {
		if (old_ws && !old_ws->node.destroying) {
			arrange_workspace(old_ws);
		}
		arrange_node(node_get_parent(destination));
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static void workspace_move_to_output(struct sway_workspace *workspace,
		struct sway_output *output) {
	if (workspace->output == output) {
		return;
	}
	struct sway_output *old_output = workspace->output;
	workspace_detach(workspace);
	struct sway_workspace *new_output_old_ws =
		output_get_active_workspace(output);
	if (!sway_assert(new_output_old_ws, "Expected output to have a workspace")) {
		return;
	}

	output_add_workspace(output, workspace);

	// If moving the last workspace from the old output, create a new workspace
	// on the old output
	struct sway_seat *seat = config->handler_context.seat;
	if (old_output->workspaces->length == 0) {
		char *ws_name = workspace_next_name(old_output->wlr_output->name);
		struct sway_workspace *ws = workspace_create(old_output, ws_name);
		free(ws_name);
		seat_set_raw_focus(seat, &ws->node);
	}

	workspace_consider_destroy(new_output_old_ws);

	output_sort_workspaces(output);
	struct sway_node *focus = seat_get_focus_inactive(seat, &workspace->node);
	seat_set_focus(seat, focus);
	workspace_output_raise_priority(workspace, old_output, output);
	ipc_event_workspace(NULL, workspace, "move");
}

static struct cmd_results *cmd_move_workspace(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move workspace", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "output") == 0) {
		--argc; ++argv;
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID,
				"Expected 'move workspace to [output] <output>'");
	}

	struct sway_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		return cmd_results_new(CMD_FAILURE, "No workspace to move");
	}

	struct sway_output *old_output = workspace->output;
	int center_x = workspace->width / 2 + workspace->x,
		center_y = workspace->height / 2 + workspace->y;
	struct sway_output *new_output = output_in_direction(argv[0],
			old_output, center_x, center_y);
	if (!new_output) {
		return cmd_results_new(CMD_FAILURE,
			"Can't find output with name/direction '%s'", argv[0]);
	}
	workspace_move_to_output(workspace, new_output);

	arrange_output(old_output);
	arrange_output(new_output);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_in_direction(
		enum wlr_direction direction, int argc, char **argv) {
	int move_amt = 10;
	if (argc) {
		char *inv;
		move_amt = (int)strtol(argv[0], &inv, 10);
		if (*inv != '\0' && strcasecmp(inv, "px") != 0) {
			return cmd_results_new(CMD_FAILURE, "Invalid distance specified");
		}
	}

	struct sway_container *container = config->handler_context.container;
	if (!container) {
		return cmd_results_new(CMD_FAILURE,
				"Cannot move workspaces in a direction");
	}
	if (container_is_floating(container)) {
		if (container->pending.fullscreen_mode) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move fullscreen floating container");
		}
		double lx = container->pending.x;
		double ly = container->pending.y;
		switch (direction) {
		case WLR_DIRECTION_LEFT:
			lx -= move_amt;
			break;
		case WLR_DIRECTION_RIGHT:
			lx += move_amt;
			break;
		case WLR_DIRECTION_UP:
			ly -= move_amt;
			break;
		case WLR_DIRECTION_DOWN:
			ly += move_amt;
			break;
		}
		container_floating_move_to(container, lx, ly);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}
	struct sway_workspace *old_ws = container->pending.workspace;
	struct sway_container *old_parent = container->pending.parent;

	if (!container_move_in_direction(container, direction)) {
		// Container didn't move
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	// clean-up, destroying parents if the container was the last child
	if (old_parent) {
		container_reap_empty(old_parent);
	} else if (old_ws) {
		workspace_consider_destroy(old_ws);
	}

	struct sway_workspace *new_ws = container->pending.workspace;

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		arrange_workspace(old_ws);
		if (new_ws != old_ws) {
			arrange_workspace(new_ws);
		}
	}

	if (container->view) {
		ipc_event_window(container, "move");
	}

	// Hack to re-focus container
	seat_set_raw_focus(config->handler_context.seat, &new_ws->node);
	seat_set_focus_container(config->handler_context.seat, container);

	if (old_ws != new_ws) {
		ipc_event_workspace(old_ws, new_ws, "focus");
		workspace_detect_urgent(old_ws);
		workspace_detect_urgent(new_ws);
	}
	container_end_mouse_operation(container);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_to_position_pointer(
		struct sway_container *container) {
	struct sway_seat *seat = config->handler_context.seat;
	if (!seat->cursor) {
		return cmd_results_new(CMD_FAILURE, "No cursor device");
	}
	struct wlr_cursor *cursor = seat->cursor->cursor;
	/* Determine where to put the window. */
	double lx = cursor->x - container->pending.width / 2;
	double ly = cursor->y - container->pending.height / 2;

	/* Correct target coordinates to be in bounds (on screen). */
	struct wlr_output *output = wlr_output_layout_output_at(
			root->output_layout, cursor->x, cursor->y);
	if (output) {
		struct wlr_box *box =
				wlr_output_layout_get_box(root->output_layout, output);
		lx = fmax(lx, box->x);
		ly = fmax(ly, box->y);
		if (lx + container->pending.width > box->x + box->width) {
			lx = box->x + box->width - container->pending.width;
		}
		if (ly + container->pending.height > box->y + box->height) {
			ly = box->y + box->height - container->pending.height;
		}
	}

	/* Actually move the container. */
	container_floating_move_to(container, lx, ly);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_position_syntax[] =
	"Expected 'move [absolute] position <x> [px] <y> [px]' or "
	"'move [absolute] position center' or "
	"'move position cursor|mouse|pointer'";

static struct cmd_results *cmd_move_to_position(int argc, char **argv) {
	struct sway_container *container = config->handler_context.container;
	if (!container || !container_is_floating(container)) {
		return cmd_results_new(CMD_FAILURE, "Only floating containers "
				"can be moved to an absolute position");
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}

	bool absolute = false;
	if (strcmp(argv[0], "absolute") == 0) {
		absolute = true;
		--argc;
		++argv;
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}
	if (strcmp(argv[0], "position") == 0) {
		--argc;
		++argv;
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}
	if (strcmp(argv[0], "cursor") == 0 || strcmp(argv[0], "mouse") == 0 ||
			strcmp(argv[0], "pointer") == 0) {
		if (absolute) {
			return cmd_results_new(CMD_INVALID, expected_position_syntax);
		}
		return cmd_move_to_position_pointer(container);
	} else if (strcmp(argv[0], "center") == 0) {
		double lx, ly;
		if (absolute) {
			lx = root->x + (root->width - container->pending.width) / 2;
			ly = root->y + (root->height - container->pending.height) / 2;
		} else {
			struct sway_workspace *ws = container->pending.workspace;
			if (!ws) {
				struct sway_seat *seat = config->handler_context.seat;
				ws = seat_get_focused_workspace(seat);
			}
			lx = ws->x + (ws->width - container->pending.width) / 2;
			ly = ws->y + (ws->height - container->pending.height) / 2;
		}
		container_floating_move_to(container, lx, ly);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (argc < 2) {
		return cmd_results_new(CMD_FAILURE, expected_position_syntax);
	}

	struct movement_amount lx = { .amount = 0, .unit = MOVEMENT_UNIT_INVALID };
	// X direction
	int num_consumed_args = parse_movement_amount(argc, argv, &lx);
	argc -= num_consumed_args;
	argv += num_consumed_args;
	if (lx.unit == MOVEMENT_UNIT_INVALID) {
		return cmd_results_new(CMD_INVALID, "Invalid x position specified");
	}

	struct movement_amount ly = { .amount = 0, .unit = MOVEMENT_UNIT_INVALID };
	// Y direction
	num_consumed_args = parse_movement_amount(argc, argv, &ly);
	argc -= num_consumed_args;
	argv += num_consumed_args;
	if (argc > 0) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}
	if (ly.unit == MOVEMENT_UNIT_INVALID) {
		return cmd_results_new(CMD_INVALID, "Invalid y position specified");
	}

	struct sway_workspace *ws = container->pending.workspace;
	if (!ws) {
		struct sway_seat *seat = config->handler_context.seat;
		ws = seat_get_focused_workspace(seat);
	}

	switch (lx.unit) {
	case MOVEMENT_UNIT_PPT:
		if (container_is_scratchpad_hidden(container)) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move a hidden scratchpad container by ppt");
		}
		if (absolute) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move to absolute positions by ppt");
		}
		// Convert to px
		lx.amount = ws->width * lx.amount / 100;
		lx.unit = MOVEMENT_UNIT_PX;
		// Falls through
	case MOVEMENT_UNIT_PX:
	case MOVEMENT_UNIT_DEFAULT:
		break;
	case MOVEMENT_UNIT_INVALID:
		sway_assert(false, "invalid x unit");
		break;
	}

	switch (ly.unit) {
	case MOVEMENT_UNIT_PPT:
		if (container_is_scratchpad_hidden(container)) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move a hidden scratchpad container by ppt");
		}
		if (absolute) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move to absolute positions by ppt");
		}
		// Convert to px
		ly.amount = ws->height * ly.amount / 100;
		ly.unit = MOVEMENT_UNIT_PX;
		// Falls through
	case MOVEMENT_UNIT_PX:
	case MOVEMENT_UNIT_DEFAULT:
		break;
	case MOVEMENT_UNIT_INVALID:
		sway_assert(false, "invalid y unit");
		break;
	}
	if (!absolute) {
		lx.amount += ws->x;
		ly.amount += ws->y;
	}
	container_floating_move_to(container, lx.amount, ly.amount);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_to_scratchpad(void) {
	struct sway_node *node = config->handler_context.node;
	struct sway_container *con = config->handler_context.container;
	struct sway_workspace *ws = config->handler_context.workspace;
	if (node->type == N_WORKSPACE && ws->tiling->length == 0) {
		return cmd_results_new(CMD_INVALID,
				"Can't move an empty workspace to the scratchpad");
	}
	if (node->type == N_WORKSPACE) {
		// Wrap the workspace's children in a container
		con = workspace_wrap_children(ws);
		ws->layout = L_HORIZ;
	}

	// If the container is in a floating split container,
	// operate on the split container instead of the child.
	if (container_is_floating_or_child(con)) {
		while (con->pending.parent) {
			con = con->pending.parent;
		}
	}

	if (!con->scratchpad) {
		root_scratchpad_add_container(con, NULL);
	} else if (con->pending.workspace) {
		root_scratchpad_hide(con);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_full_syntax[] = "Expected "
	"'move left|right|up|down [<amount> [px]]'"
	" or 'move [--no-auto-back-and-forth] [window|container] [to] workspace"
	"  <name>|next|prev|next_on_output|prev_on_output|current|(number <num>)'"
	" or 'move [window|container] [to] output <name/id>|left|right|up|down'"
	" or 'move [window|container] [to] mark <mark>'"
	" or 'move [window|container] [to] scratchpad'"
	" or 'move workspace to [output] <name/id>|left|right|up|down'"
	" or 'move [window|container] [to] [absolute] position <x> [px] <y> [px]'"
	" or 'move [window|container] [to] [absolute] position center'"
	" or 'move [window|container] [to] position mouse|cursor|pointer'";

struct cmd_results *cmd_move(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}

	if (strcasecmp(argv[0], "left") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_LEFT, --argc, ++argv);
	} else if (strcasecmp(argv[0], "right") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_RIGHT, --argc, ++argv);
	} else if (strcasecmp(argv[0], "up") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_UP, --argc, ++argv);
	} else if (strcasecmp(argv[0], "down") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_DOWN, --argc, ++argv);
	} else if (strcasecmp(argv[0], "workspace") == 0 && argc >= 2
			&& (strcasecmp(argv[1], "to") == 0 ||
				strcasecmp(argv[1], "output") == 0)) {
		argc -= 2; argv += 2;
		return cmd_move_workspace(argc, argv);
	}

	bool no_auto_back_and_forth = false;
	if (strcasecmp(argv[0], "--no-auto-back-and-forth") == 0) {
		no_auto_back_and_forth = true;
		--argc; ++argv;
	}

	if (argc > 0 && (strcasecmp(argv[0], "window") == 0 ||
			strcasecmp(argv[0], "container") == 0)) {
		--argc; ++argv;
	}

	if (argc > 0 && strcasecmp(argv[0], "to") == 0) {
		--argc;	++argv;
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_full_syntax);
	}

	// Only `move [window|container] [to] workspace` supports
	// `--no-auto-back-and-forth` so treat others as invalid syntax
	if (no_auto_back_and_forth && strcasecmp(argv[0], "workspace") != 0) {
		return cmd_results_new(CMD_INVALID, expected_full_syntax);
	}

	if (strcasecmp(argv[0], "workspace") == 0 ||
			strcasecmp(argv[0], "output") == 0 ||
			strcasecmp(argv[0], "mark") == 0) {
		return cmd_move_container(no_auto_back_and_forth, argc, argv);
	} else if (strcasecmp(argv[0], "scratchpad") == 0) {
		return cmd_move_to_scratchpad();
	} else if (strcasecmp(argv[0], "position") == 0 ||
			(argc > 1 && strcasecmp(argv[0], "absolute") == 0 &&
			strcasecmp(argv[1], "position") == 0)) {
		return cmd_move_to_position(argc, argv);
	}
	return cmd_results_new(CMD_INVALID, expected_full_syntax);
}
