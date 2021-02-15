#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "sway/client_label.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/server.h"
#include "sway/tree/container.h"
#include "sway/tree/root.h"
#include "sway/tree/workspace.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

struct cmd_results *cmd_exec_validate(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, argv[-1], EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (!config->active || config->validating) {
		return cmd_results_new(CMD_DEFER, NULL);
	}
	return error;
}

struct cmd_results *cmd_exec_process(int argc, char **argv) {
	struct cmd_results *error = NULL;
	bool use_wl_socket = false;
	int skip_argc = 0;
	char *tmp = NULL;
	char *label = NULL;

	while (skip_argc < argc && strncmp(argv[skip_argc], "--", 2) == 0) {
		if (strcmp(argv[skip_argc], "--no-startup-id") == 0) {
			sway_log(SWAY_INFO, "exec switch '--no-startup-id' not supported, ignored.");
			skip_argc++;
		} else if (strcmp(argv[skip_argc], "--use-wayland-socket") == 0) {
			use_wl_socket = true;
			skip_argc++;
		} else if (strcmp(argv[skip_argc], "--label") == 0) {
			skip_argc++;
			if (skip_argc >= argc)
				return cmd_results_new(CMD_INVALID, "--label requires an argument");
			label = argv[skip_argc];
			skip_argc++;
		} else if (strcmp(argv[skip_argc], "--") == 0) {
			skip_argc++;
			break;
		} else {
			return cmd_results_new(CMD_INVALID, "Unknown switch %s", argv[skip_argc]);
		}
	}
	if ((error = checkarg(argc - skip_argc, argv[-1], EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	argc -= skip_argc;
	argv += skip_argc;

	if (label)
		use_wl_socket = true;

	if (argc == 1 && (argv[0][0] == '\'' || argv[0][0] == '"')) {
		tmp = strdup(argv[0]);
		strip_quotes(tmp);
	} else {
		tmp = join_args(argv, argc);
	}

	// Put argument into cmd array
	char cmd[4096];
	strncpy(cmd, tmp, sizeof(cmd) - 1);
	cmd[sizeof(cmd) - 1] = 0;
	free(tmp);
	sway_log(SWAY_DEBUG, "Executing %s", cmd);

	int fd[2];
	if (pipe(fd) != 0) {
		sway_log(SWAY_ERROR, "Unable to create pipe for fork");
	}

	int sockets[2];
	struct wl_client* client = NULL;
	if (use_wl_socket) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
			sway_log_errno(SWAY_ERROR, "socketpair failed in exec");
			use_wl_socket = false;
		} else {
			sway_set_cloexec(sockets[0], true);
		}
	}

	pid_t pid, child;
	// Fork process
	if ((pid = fork()) == 0) {
		// Fork child process again
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		close(fd[0]);
		if (use_wl_socket) {
			close(sockets[0]);
			sway_set_cloexec(sockets[1], false);

			char wayland_socket_str[16];
			snprintf(wayland_socket_str, sizeof(wayland_socket_str),
					"%d", sockets[1]);
			setenv("WAYLAND_SOCKET", wayland_socket_str, true);
		}
		if ((child = fork()) == 0) {
			close(fd[1]);
			execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
			_exit(0);
		}
		ssize_t s = 0;
		while ((size_t)s < sizeof(pid_t)) {
			s += write(fd[1], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
		}
		close(fd[1]);
		_exit(0); // Close child process
	} else if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		if (use_wl_socket) {
			close(sockets[0]);
			close(sockets[1]);
		}
		return cmd_results_new(CMD_FAILURE, "fork() failed");
	}
	close(fd[1]); // close write
	if (use_wl_socket) {
		close(sockets[1]);
		client = wl_client_create(server.wl_display, sockets[0]);
	}
	ssize_t s = 0;
	while ((size_t)s < sizeof(pid_t)) {
		s += read(fd[0], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
	}
	close(fd[0]);
	// cleanup child process
	waitpid(pid, NULL, 0);
	if (child > 0) {
		sway_log(SWAY_DEBUG, "Child process created with pid %d", child);
		root_record_workspace_pid(child);
	} else {
		return cmd_results_new(CMD_FAILURE, "Second fork() failed");
	}

	if (client && label) {
		wl_client_label_set(client, strdup(label));
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_exec_always(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = cmd_exec_validate(argc, argv))) {
		return error;
	}
	return cmd_exec_process(argc, argv);
}
