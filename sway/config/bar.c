#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>
#include "sway/client_label.h"
#include "sway/config.h"
#include "sway/input/keyboard.h"
#include "sway/output.h"
#include "config.h"
#include "list.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

void free_bar_binding(struct bar_binding *binding) {
	if (!binding) {
		return;
	}
	free(binding->command);
	free(binding);
}

void free_bar_config(struct bar_config *bar) {
	if (!bar) {
		return;
	}
	free(bar->id);
	free(bar->mode);
	free(bar->position);
	free(bar->hidden_state);
	free(bar->status_command);
	free(bar->swaybar_command);
	free(bar->swaybar_label);
	free(bar->font);
	free(bar->separator_symbol);
	if (bar->bindings) {
		for (int i = 0; i < bar->bindings->length; i++) {
			free_bar_binding(bar->bindings->items[i]);
		}
	}
	list_free(bar->bindings);
	list_free_items_and_destroy(bar->outputs);
	if (bar->client != NULL) {
		wl_client_destroy(bar->client);
	}
	free(bar->colors.background);
	free(bar->colors.statusline);
	free(bar->colors.separator);
	free(bar->colors.focused_background);
	free(bar->colors.focused_statusline);
	free(bar->colors.focused_separator);
	free(bar->colors.focused_workspace_border);
	free(bar->colors.focused_workspace_bg);
	free(bar->colors.focused_workspace_text);
	free(bar->colors.active_workspace_border);
	free(bar->colors.active_workspace_bg);
	free(bar->colors.active_workspace_text);
	free(bar->colors.inactive_workspace_border);
	free(bar->colors.inactive_workspace_bg);
	free(bar->colors.inactive_workspace_text);
	free(bar->colors.urgent_workspace_border);
	free(bar->colors.urgent_workspace_bg);
	free(bar->colors.urgent_workspace_text);
	free(bar->colors.binding_mode_border);
	free(bar->colors.binding_mode_bg);
	free(bar->colors.binding_mode_text);
#if HAVE_TRAY
	list_free_items_and_destroy(bar->tray_outputs);
	free(bar->icon_theme);

	struct tray_binding *tray_bind = NULL, *tmp_tray_bind = NULL;
	wl_list_for_each_safe(tray_bind, tmp_tray_bind, &bar->tray_bindings, link) {
		wl_list_remove(&tray_bind->link);
		free(tray_bind);
	}
#endif
	free(bar);
}

struct bar_config *default_bar_config(void) {
	struct bar_config *bar = NULL;
	bar = calloc(1, sizeof(struct bar_config));
	if (!bar) {
		return NULL;
	}
	bar->outputs = NULL;
	bar->position = strdup("bottom");
	bar->pango_markup = false;
	bar->swaybar_command = NULL;
	bar->font = NULL;
	bar->height = 0;
	bar->workspace_buttons = true;
	bar->wrap_scroll = false;
	bar->separator_symbol = NULL;
	bar->strip_workspace_numbers = false;
	bar->strip_workspace_name = false;
	bar->binding_mode_indicator = true;
	bar->verbose = false;
	bar->modifier = get_modifier_mask_by_name("Mod4");
	bar->status_padding = 1;
	bar->status_edge_padding = 3;
	bar->workspace_min_width = 0;
	if (!(bar->mode = strdup("dock"))) {
	       goto cleanup;
	}
	if (!(bar->hidden_state = strdup("hide"))) {
		goto cleanup;
	}
	if (!(bar->bindings = create_list())) {
		goto cleanup;
	}
	// set default colors
	if (!(bar->colors.background = strndup("#000000ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.statusline = strndup("#ffffffff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.separator = strndup("#666666ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.focused_workspace_border = strndup("#4c7899ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.focused_workspace_bg = strndup("#285577ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.focused_workspace_text = strndup("#ffffffff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.active_workspace_border = strndup("#333333ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.active_workspace_bg = strndup("#5f676aff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.active_workspace_text = strndup("#ffffffff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.inactive_workspace_border = strndup("#333333ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.inactive_workspace_bg = strndup("#222222ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.inactive_workspace_text = strndup("#888888ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.urgent_workspace_border = strndup("#2f343aff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.urgent_workspace_bg = strndup("#900000ff", 9))) {
		goto cleanup;
	}
	if (!(bar->colors.urgent_workspace_text = strndup("#ffffffff", 9))) {
		goto cleanup;
	}
	// if the following colors stay undefined, they fall back to background,
	// statusline, separator and urgent_workspace_*.
	bar->colors.focused_background = NULL;
	bar->colors.focused_statusline = NULL;
	bar->colors.focused_separator = NULL;
	bar->colors.binding_mode_border = NULL;
	bar->colors.binding_mode_bg = NULL;
	bar->colors.binding_mode_text = NULL;

#if HAVE_TRAY
	bar->tray_padding = 2;
	wl_list_init(&bar->tray_bindings);
#endif

	return bar;
cleanup:
	free_bar_config(bar);
	return NULL;
}

static void handle_swaybar_client_destroy(struct wl_listener *listener,
		void *data) {
	struct bar_config *bar = wl_container_of(listener, bar, client_destroy);
	wl_list_remove(&bar->client_destroy.link);
	wl_list_init(&bar->client_destroy.link);
	bar->client = NULL;
}

static void invoke_swaybar(struct bar_config *bar) {
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		sway_log_errno(SWAY_ERROR, "socketpair failed");
		return;
	}
	if (!sway_set_cloexec(sockets[0], true) || !sway_set_cloexec(sockets[1], true)) {
		return;
	}

	bar->client = wl_client_create(server.wl_display, sockets[0]);
	if (bar->client == NULL) {
		sway_log_errno(SWAY_ERROR, "wl_client_create failed");
		return;
	}

	if (bar->swaybar_label) {
		wl_client_label_set(bar->client, strdup(bar->swaybar_label));
	}

	bar->client_destroy.notify = handle_swaybar_client_destroy;
	wl_client_add_destroy_listener(bar->client, &bar->client_destroy);

	pid_t pid = fork();
	if (pid < 0) {
		sway_log(SWAY_ERROR, "Failed to create fork for swaybar");
		return;
	} else if (pid == 0) {
		// Remove the SIGUSR1 handler that wlroots adds for xwayland
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);

		pid = fork();
		if (pid < 0) {
			sway_log_errno(SWAY_ERROR, "fork failed");
			_exit(EXIT_FAILURE);
		} else if (pid == 0) {
			if (!sway_set_cloexec(sockets[1], false)) {
				_exit(EXIT_FAILURE);
			}

			char wayland_socket_str[16];
			snprintf(wayland_socket_str, sizeof(wayland_socket_str),
					"%d", sockets[1]);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);

			// run custom swaybar
			char *const cmd[] = {
					bar->swaybar_command ? bar->swaybar_command : "swaybar",
					"-b", bar->id, NULL};
			execvp(cmd[0], cmd);
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}

	if (close(sockets[1]) != 0) {
		sway_log_errno(SWAY_ERROR, "close failed");
		return;
	}

	if (waitpid(pid, NULL, 0) < 0) {
		sway_log_errno(SWAY_ERROR, "waitpid failed");
		return;
	}

	sway_log(SWAY_DEBUG, "Spawned swaybar %s", bar->id);
	return;
}

void load_swaybar(struct bar_config *bar) {
	if (bar->client != NULL) {
		wl_client_destroy(bar->client);
	}
	sway_log(SWAY_DEBUG, "Invoking swaybar for bar id '%s'", bar->id);
	invoke_swaybar(bar);
}

void load_swaybars(void) {
	for (int i = 0; i < config->bars->length; ++i) {
		struct bar_config *bar = config->bars->items[i];
		load_swaybar(bar);
	}
}
