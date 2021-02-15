#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "log.h"
#include "sway/client_label.h"
#include "sway/config.h"
#include "sway/server.h"
#include "sway/swaynag.h"
#include "util.h"

static void handle_swaynag_client_destroy(struct wl_listener *listener,
		void *data) {
	struct swaynag_instance *swaynag =
		wl_container_of(listener, swaynag, client_destroy);
	wl_list_remove(&swaynag->client_destroy.link);
	wl_list_init(&swaynag->client_destroy.link);
	swaynag->client = NULL;
}

bool swaynag_spawn(const char *swaynag_command,
		struct swaynag_instance *swaynag) {
	if (swaynag->client != NULL) {
		wl_client_destroy(swaynag->client);
	}

	if (!swaynag_command) {
		return true;
	}

	if (swaynag->detailed) {
		if (pipe(swaynag->fd) != 0) {
			sway_log(SWAY_ERROR, "Failed to create pipe for swaynag");
			return false;
		}
		if (!sway_set_cloexec(swaynag->fd[1], true)) {
			goto failed;
		}
	}

	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
		sway_log_errno(SWAY_ERROR, "socketpair failed");
		goto failed;
	}
	if (!sway_set_cloexec(sockets[0], true) || !sway_set_cloexec(sockets[1], true)) {
		goto failed;
	}

	swaynag->client = wl_client_create(server.wl_display, sockets[0]);
	if (swaynag->client == NULL) {
		sway_log_errno(SWAY_ERROR, "wl_client_create failed");
		goto failed;
	}

	if (config->swaynag_label) {
		wl_client_label_set(swaynag->client, strdup(config->swaynag_label));
	}

	swaynag->client_destroy.notify = handle_swaynag_client_destroy;
	wl_client_add_destroy_listener(swaynag->client, &swaynag->client_destroy);

	pid_t pid = fork();
	if (pid < 0) {
		sway_log(SWAY_ERROR, "Failed to create fork for swaynag");
		goto failed;
	} else if (pid == 0) {
		pid = fork();
		if (pid < 0) {
			sway_log_errno(SWAY_ERROR, "fork failed");
			_exit(EXIT_FAILURE);
		} else if (pid == 0) {
			if (!sway_set_cloexec(sockets[1], false)) {
				_exit(EXIT_FAILURE);
			}

			if (swaynag->detailed) {
				close(swaynag->fd[1]);
				dup2(swaynag->fd[0], STDIN_FILENO);
				close(swaynag->fd[0]);
			}

			char wayland_socket_str[16];
			snprintf(wayland_socket_str, sizeof(wayland_socket_str),
					"%d", sockets[1]);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);

			size_t length = strlen(swaynag_command) + strlen(swaynag->args) + 2;
			char *cmd = malloc(length);
			snprintf(cmd, length, "%s %s", swaynag_command, swaynag->args);
			execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
			sway_log_errno(SWAY_ERROR, "execl failed");
			_exit(EXIT_FAILURE);
		}
		_exit(EXIT_SUCCESS);
	}

	if (swaynag->detailed) {
		if (close(swaynag->fd[0]) != 0) {
			sway_log_errno(SWAY_ERROR, "close failed");
			return false;
		}
	}

	if (close(sockets[1]) != 0) {
		sway_log_errno(SWAY_ERROR, "close failed");
		return false;
	}

	if (waitpid(pid, NULL, 0) < 0) {
		sway_log_errno(SWAY_ERROR, "waitpid failed");
		return false;
	}

	return true;

failed:
	if (swaynag->detailed) {
		if (close(swaynag->fd[0]) != 0) {
			sway_log_errno(SWAY_ERROR, "close failed");
			return false;
		}
		if (close(swaynag->fd[1]) != 0) {
			sway_log_errno(SWAY_ERROR, "close failed");
		}
	}
	return false;
}

void swaynag_log(const char *swaynag_command, struct swaynag_instance *swaynag,
		const char *fmt, ...) {
	if (!swaynag_command) {
		return;
	}

	if (!swaynag->detailed) {
		sway_log(SWAY_ERROR, "Attempting to write to non-detailed swaynag inst");
		return;
	}

	if (swaynag->client == NULL && !swaynag_spawn(swaynag_command, swaynag)) {
		return;
	}

	va_list args;
	va_start(args, fmt);
	size_t length = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	char *temp = malloc(length + 1);
	if (!temp) {
		sway_log(SWAY_ERROR, "Failed to allocate buffer for swaynag log entry.");
		return;
	}

	va_start(args, fmt);
	vsnprintf(temp, length, fmt, args);
	va_end(args);

	write(swaynag->fd[1], temp, length);

	free(temp);
}

void swaynag_show(struct swaynag_instance *swaynag) {
	if (swaynag->detailed && swaynag->client != NULL) {
		close(swaynag->fd[1]);
	}
}

