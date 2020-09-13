#define _XOPEN_SOURCE 700 // for strdup
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <wayland-server-core.h>
#include "sway/client_label.h"
#include "sway/commands.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct sandbox_socket {
	char* path;
	struct wl_event_source *src;
	int fd;
	char* label;
};

static list_t *sandbox_sockets;

static int fd_accept(int srv_fd, uint32_t mask, void *data) {
	struct sandbox_socket *sock = data;

	int cli_fd = accept(srv_fd, NULL, NULL);
	if (cli_fd < 0) {
		if (errno == EINTR || errno == ECONNABORTED || errno == EAGAIN || errno == EWOULDBLOCK) {
			return 1;
		} else {
			int i;
			wl_event_source_remove(sock->src);
			unlink(sock->path);
			free(sock->path);
			close(srv_fd);
			for(i = 0; i < sandbox_sockets->length; ++i) {
				if (sock != sandbox_sockets->items[i])
					continue;
				list_del(sandbox_sockets, i);
				break;
			}
			free(sock);
			return 0;
		}
	}

	sway_set_cloexec(cli_fd, true);
	struct wl_client* client = wl_client_create(server.wl_display, cli_fd);
	if (client) {
		wl_client_label_set(client, strdup(sock->label));
	} else {
		close(cli_fd);
	}

	return 1;
}

struct cmd_results *cmd_sandbox_socket(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "sandbox_socket", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	if (!sandbox_sockets)
		sandbox_sockets = create_list();

	char* op = argv[0];

	if (strcmp(op, "create") == 0) {
		struct sockaddr_un name = {};
		char* label = NULL;
		int i = 1;
		while (i < argc) {
			if (strcmp(argv[i], "--label") == 0) {
				if (i + 1 >= argc)
					return cmd_results_new(CMD_INVALID, "--label requires an argument");
				label = argv[i + 1];
				i += 2;
			} else if (strcmp(argv[i], "--") == 0) {  // after this any argument should be treated as positional
				++i;
				break;
			} else if (strncmp(argv[i], "-", 1) == 0) {
				return cmd_results_new(CMD_INVALID, "Unknown option to sandbox_socket");
			} else {
				break;  // end of options, now only positional arguments
			}
		}

		if ((error = checkarg(argc, "sandbox_socket", EXPECTED_EQUAL_TO, i + 1))) {
			return error;
		}
		char *path = argv[i];
		size_t path_len = strlen(path) + 1;

		if (path_len > sizeof(name.sun_path)) {
			return cmd_results_new(CMD_INVALID, "Invalid socket path: %s", path);
		}
		unlink(path);

		name.sun_family = AF_UNIX;
		memcpy(name.sun_path, path, path_len);
		size_t name_len = offsetof(struct sockaddr_un, sun_path) + path_len;

		int srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (srv_fd < 0 ||
				!sway_set_cloexec(srv_fd, true) ||
				fcntl(srv_fd, F_SETFL, O_NONBLOCK) != 0 ||
				bind(srv_fd, (struct sockaddr *)&name, name_len) != 0 ||
				listen(srv_fd, 5) != 0) {
			close(srv_fd);
			return cmd_results_new(CMD_FAILURE, "Error creating socket: %s", strerror(errno));
		}

		struct sandbox_socket *sock = calloc(1, sizeof(*sock));
		sock->path = strdup(path);
		sock->src = wl_event_loop_add_fd(server.wl_event_loop, srv_fd, WL_EVENT_READABLE, fd_accept, sock);
		sock->fd = srv_fd;
		if (label)
			sock->label = strdup(label);

		list_add(sandbox_sockets, sock);
		return cmd_results_new(CMD_SUCCESS, NULL);
	} else if (strcmp(op, "delete") == 0) {
		char* path = argv[1];
		int i;
		for(i = 0; i < sandbox_sockets->length; ++i) {
			struct sandbox_socket *sock = sandbox_sockets->items[i];
			if (strcmp(sock->path, path) == 0) {
				wl_event_source_remove(sock->src);
				unlink(path);
				close(sock->fd);
				free(sock->path);
				free(sock->label);
				free(sock);
				list_del(sandbox_sockets, i);
				return cmd_results_new(CMD_SUCCESS, NULL);
			}
		}
		return cmd_results_new(CMD_FAILURE, "sandbox_socket: %s not found", path);
	} else {
		return cmd_results_new(CMD_INVALID, "Unknown command sandbox_socket %s", op);
	}
}
