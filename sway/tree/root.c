#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output_layout.h>
#include "sway/desktop/transaction.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct sway_root *root;

static void output_layout_handle_change(struct wl_listener *listener,
		void *data) {
	arrange_root();
	transaction_commit_dirty();
}

struct sway_root *root_create(void) {
	struct sway_root *root = calloc(1, sizeof(struct sway_root));
	if (!root) {
		sway_log(SWAY_ERROR, "Unable to allocate sway_root");
		return NULL;
	}
	node_init(&root->node, N_ROOT, root);
	root->output_layout = wlr_output_layout_create();
	wl_list_init(&root->all_outputs);
#if HAVE_XWAYLAND
	wl_list_init(&root->xwayland_unmanaged);
#endif
	wl_list_init(&root->drag_icons);
	wl_signal_init(&root->events.new_node);
	root->outputs = create_list();
	root->scratchpad = create_list();

	root->output_layout_change.notify = output_layout_handle_change;
	wl_signal_add(&root->output_layout->events.change,
		&root->output_layout_change);
	return root;
}

void root_destroy(struct sway_root *root) {
	wl_list_remove(&root->output_layout_change.link);
	list_free(root->scratchpad);
	list_free(root->outputs);
	wlr_output_layout_destroy(root->output_layout);
	free(root);
}

void root_scratchpad_add_container(struct sway_container *con, struct sway_workspace *ws) {
	if (!sway_assert(!con->scratchpad, "Container is already in scratchpad")) {
		return;
	}

	struct sway_container *parent = con->pending.parent;
	struct sway_workspace *workspace = con->pending.workspace;

	// Clear the fullscreen mode when sending to the scratchpad
	if (con->pending.fullscreen_mode != FULLSCREEN_NONE) {
		container_fullscreen_disable(con);
	}

	// When a tiled window is sent to scratchpad, center and resize it.
	if (!container_is_floating(con)) {
		container_set_floating(con, true);
		container_floating_set_default_size(con);
		container_floating_move_to_center(con);
	}

	container_detach(con);
	con->scratchpad = true;
	list_add(root->scratchpad, con);
	if (ws) {
		workspace_add_floating(ws, con);
	}

	if (!ws) {
		struct sway_seat *seat = input_manager_current_seat();
		struct sway_node *new_focus = NULL;
		if (parent) {
			arrange_container(parent);
			new_focus = seat_get_focus_inactive(seat, &parent->node);
		}
		if (!new_focus) {
			arrange_workspace(workspace);
			new_focus = seat_get_focus_inactive(seat, &workspace->node);
		}
		seat_set_focus(seat, new_focus);
	}

	ipc_event_window(con, "move");
}

void root_scratchpad_remove_container(struct sway_container *con) {
	if (!sway_assert(con->scratchpad, "Container is not in scratchpad")) {
		return;
	}
	con->scratchpad = false;
	int index = list_find(root->scratchpad, con);
	if (index != -1) {
		list_del(root->scratchpad, index);
		ipc_event_window(con, "move");
	}
}

void root_scratchpad_show(struct sway_container *con) {
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_workspace *new_ws = seat_get_focused_workspace(seat);
	if (!new_ws) {
		sway_log(SWAY_DEBUG, "No focused workspace to show scratchpad on");
		return;
	}
	struct sway_workspace *old_ws = con->pending.workspace;

	// If the current con or any of its parents are in fullscreen mode, we
	// first need to disable it before showing the scratchpad con.
	if (new_ws->fullscreen) {
		container_fullscreen_disable(new_ws->fullscreen);
	}
	if (root->fullscreen_global) {
		container_fullscreen_disable(root->fullscreen_global);
	}

	// Show the container
	if (old_ws) {
		container_detach(con);
		workspace_consider_destroy(old_ws);
	} else {
		// Act on the ancestor of scratchpad hidden split containers
		while (con->pending.parent) {
			con = con->pending.parent;
		}
	}
	workspace_add_floating(new_ws, con);

	// Make sure the container's center point overlaps this workspace
	double center_lx = con->pending.x + con->pending.width / 2;
	double center_ly = con->pending.y + con->pending.height / 2;

	struct wlr_box workspace_box;
	workspace_get_box(new_ws, &workspace_box);
	if (!wlr_box_contains_point(&workspace_box, center_lx, center_ly)) {
		container_floating_resize_and_center(con);
	}

	arrange_workspace(new_ws);
	seat_set_focus(seat, seat_get_focus_inactive(seat, &con->node));
}

static void disable_fullscreen(struct sway_container *con, void *data) {
	if (con->pending.fullscreen_mode != FULLSCREEN_NONE) {
		container_fullscreen_disable(con);
	}
}

void root_scratchpad_hide(struct sway_container *con) {
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_node *focus = seat_get_focus_inactive(seat, &root->node);
	struct sway_workspace *ws = con->pending.workspace;

	if (con->pending.fullscreen_mode == FULLSCREEN_GLOBAL && !con->pending.workspace) {
		// If the container was made fullscreen global while in the scratchpad,
		// it should be shown until fullscreen has been disabled
		return;
	}

	disable_fullscreen(con, NULL);
	container_for_each_child(con, disable_fullscreen, NULL);
	container_detach(con);
	arrange_workspace(ws);
	if (&con->node == focus || node_has_ancestor(focus, &con->node)) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &ws->node));
	}
	list_move_to_end(root->scratchpad, con);

	ipc_event_window(con, "move");
}

struct pid_workspace {
	pid_t pid;
	char *workspace;
	struct timespec time_added;

	struct sway_output *output;
	struct wl_listener output_destroy;

	struct wl_list link;
};

static struct wl_list pid_workspaces;

/**
 * Get the pid of a parent process given the pid of a child process.
 *
 * Returns the parent pid or NULL if the parent pid cannot be determined.
 */
static pid_t get_parent_pid(pid_t child) {
	pid_t parent = -1;
	char file_name[100];
	char *buffer = NULL;
	const char *sep = " ";
	FILE *stat = NULL;
	size_t buf_size = 0;

	sprintf(file_name, "/proc/%d/stat", child);

	if ((stat = fopen(file_name, "r"))) {
		if (getline(&buffer, &buf_size, stat) != -1) {
			strtok(buffer, sep); // pid
			strtok(NULL, sep);   // executable name
			strtok(NULL, sep);   // state
			char *token = strtok(NULL, sep);   // parent pid
			parent = strtol(token, NULL, 10);
		}
		free(buffer);
		fclose(stat);
	}

	if (parent) {
		return (parent == child) ? -1 : parent;
	}

	return -1;
}

static void pid_workspace_destroy(struct pid_workspace *pw) {
	wl_list_remove(&pw->output_destroy.link);
	wl_list_remove(&pw->link);
	free(pw->workspace);
	free(pw);
}

struct sway_workspace *root_workspace_for_pid(pid_t pid) {
	if (!pid_workspaces.prev && !pid_workspaces.next) {
		wl_list_init(&pid_workspaces);
		return NULL;
	}

	struct sway_workspace *ws = NULL;
	struct pid_workspace *pw = NULL;

	sway_log(SWAY_DEBUG, "Looking up workspace for pid %d", pid);

	do {
		struct pid_workspace *_pw = NULL;
		wl_list_for_each(_pw, &pid_workspaces, link) {
			if (pid == _pw->pid) {
				pw = _pw;
				sway_log(SWAY_DEBUG,
						"found pid_workspace for pid %d, workspace %s",
						pid, pw->workspace);
				goto found;
			}
		}
		pid = get_parent_pid(pid);
	} while (pid > 1);
found:

	if (pw && pw->workspace) {
		ws = workspace_by_name(pw->workspace);

		if (!ws) {
			sway_log(SWAY_DEBUG,
					"Creating workspace %s for pid %d because it disappeared",
					pw->workspace, pid);

			struct sway_output *output = pw->output;
			if (pw->output && !pw->output->enabled) {
				sway_log(SWAY_DEBUG,
						"Workspace output %s is disabled, trying another one",
						pw->output->wlr_output->name);
				output = NULL;
			}

			ws = workspace_create(output, pw->workspace);
		}

		pid_workspace_destroy(pw);
	}

	return ws;
}

static void pw_handle_output_destroy(struct wl_listener *listener, void *data) {
	struct pid_workspace *pw = wl_container_of(listener, pw, output_destroy);
	pw->output = NULL;
	wl_list_remove(&pw->output_destroy.link);
	wl_list_init(&pw->output_destroy.link);
}

void root_record_workspace_pid(pid_t pid) {
	sway_log(SWAY_DEBUG, "Recording workspace for process %d", pid);
	if (!pid_workspaces.prev && !pid_workspaces.next) {
		wl_list_init(&pid_workspaces);
	}

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_workspace *ws = seat_get_focused_workspace(seat);
	if (!ws) {
		sway_log(SWAY_DEBUG, "Bailing out, no workspace");
		return;
	}
	struct sway_output *output = ws->output;
	if (!output) {
		sway_log(SWAY_DEBUG, "Bailing out, no output");
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	// Remove expired entries
	static const int timeout = 60;
	struct pid_workspace *old, *_old;
	wl_list_for_each_safe(old, _old, &pid_workspaces, link) {
		if (now.tv_sec - old->time_added.tv_sec >= timeout) {
			pid_workspace_destroy(old);
		}
	}

	struct pid_workspace *pw = calloc(1, sizeof(struct pid_workspace));
	pw->workspace = strdup(ws->name);
	pw->output = output;
	pw->pid = pid;
	memcpy(&pw->time_added, &now, sizeof(struct timespec));
	pw->output_destroy.notify = pw_handle_output_destroy;
	wl_signal_add(&output->wlr_output->events.destroy, &pw->output_destroy);
	wl_list_insert(&pid_workspaces, &pw->link);
}

void root_remove_workspace_pid(pid_t pid) {
	if (!pid_workspaces.prev || !pid_workspaces.next) {
		return;
	}

	struct pid_workspace *pw, *tmp;
	wl_list_for_each_safe(pw, tmp, &pid_workspaces, link) {
		if (pid == pw->pid) {
			pid_workspace_destroy(pw);
			return;
		}
	}
}

void root_for_each_workspace(void (*f)(struct sway_workspace *ws, void *data),
		void *data) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_for_each_workspace(output, f, data);
	}
}

void root_for_each_container(void (*f)(struct sway_container *con, void *data),
		void *data) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_for_each_container(output, f, data);
	}

	// Scratchpad
	for (int i = 0; i < root->scratchpad->length; ++i) {
		struct sway_container *container = root->scratchpad->items[i];
		if (container_is_scratchpad_hidden(container)) {
			f(container, data);
			container_for_each_child(container, f, data);
		}
	}

	// Saved workspaces
	for (int i = 0; i < root->noop_output->workspaces->length; ++i) {
		struct sway_workspace *ws = root->noop_output->workspaces->items[i];
		workspace_for_each_container(ws, f, data);
	}
}

struct sway_output *root_find_output(
		bool (*test)(struct sway_output *output, void *data), void *data) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if (test(output, data)) {
			return output;
		}
	}
	return NULL;
}

struct sway_workspace *root_find_workspace(
		bool (*test)(struct sway_workspace *ws, void *data), void *data) {
	struct sway_workspace *result = NULL;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if ((result = output_find_workspace(output, test, data))) {
			return result;
		}
	}
	return NULL;
}

struct sway_container *root_find_container(
		bool (*test)(struct sway_container *con, void *data), void *data) {
	struct sway_container *result = NULL;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		if ((result = output_find_container(output, test, data))) {
			return result;
		}
	}

	// Scratchpad
	for (int i = 0; i < root->scratchpad->length; ++i) {
		struct sway_container *container = root->scratchpad->items[i];
		if (container_is_scratchpad_hidden(container)) {
			if (test(container, data)) {
				return container;
			}
			if ((result = container_find_child(container, test, data))) {
				return result;
			}
		}
	}

	// Saved workspaces
	for (int i = 0; i < root->noop_output->workspaces->length; ++i) {
		struct sway_workspace *ws = root->noop_output->workspaces->items[i];
		if ((result = workspace_find_container(ws, test, data))) {
			return result;
		}
	}

	return NULL;
}

void root_get_box(struct sway_root *root, struct wlr_box *box) {
	box->x = root->x;
	box->y = root->y;
	box->width = root->width;
	box->height = root->height;
}

void root_rename_pid_workspaces(const char *old_name, const char *new_name) {
	if (!pid_workspaces.prev && !pid_workspaces.next) {
		wl_list_init(&pid_workspaces);
	}

	struct pid_workspace *pw = NULL;
	wl_list_for_each(pw, &pid_workspaces, link) {
		if (strcmp(pw->workspace, old_name) == 0) {
			free(pw->workspace);
			pw->workspace = strdup(new_name);
		}
	}
}
