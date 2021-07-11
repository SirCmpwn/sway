// See https://i3wm.org/docs/ipc.html for protocol information
#define _POSIX_C_SOURCE 200112L
#include <linux/input-event-codes.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <json.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/desktop/transaction.h"
#include "sway/ipc-json.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/input/input-manager.h"
#include "sway/input/keyboard.h"
#include "sway/input/seat.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "util.h"

static int ipc_socket = -1;
static struct wl_event_source *ipc_event_source =  NULL;
static struct sockaddr_un *ipc_sockaddr = NULL;
static list_t *ipc_client_list = NULL;
static struct wl_listener ipc_display_destroy;

static const char ipc_magic[] = {'i', '3', '-', 'i', 'p', 'c'};

#define IPC_HEADER_SIZE (sizeof(ipc_magic) + 8)

struct ipc_client {
	struct wl_event_source *event_source;
	struct wl_event_source *writable_event_source;
	struct sway_server *server;
	int fd;
	enum ipc_command_type subscribed_events;
	size_t read_buffer_len;
	size_t read_buffer_size;
	char *read_buffer;
	size_t write_buffer_len;
	size_t write_buffer_size;
	char *write_buffer;
	// The following are for storing data between event_loop calls
	uint32_t pending_length;
	enum ipc_command_type pending_type;
};

struct sockaddr_un *ipc_user_sockaddr(void);
int ipc_handle_connection(int fd, uint32_t mask, void *data);
int ipc_client_handle_readable(int client_fd, uint32_t mask, void *data);
int ipc_client_handle_writable(int client_fd, uint32_t mask, void *data);
void ipc_client_disconnect(struct ipc_client *client);
void ipc_client_handle_command(struct ipc_client *client, uint32_t payload_length,
	enum ipc_command_type payload_type);
bool ipc_send_reply(struct ipc_client *client, enum ipc_command_type payload_type,
	const char *payload, uint32_t payload_length);

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	if (ipc_event_source) {
		wl_event_source_remove(ipc_event_source);
	}
	close(ipc_socket);
	unlink(ipc_sockaddr->sun_path);

	while (ipc_client_list->length) {
		ipc_client_disconnect(ipc_client_list->items[ipc_client_list->length-1]);
	}
	list_free(ipc_client_list);

	free(ipc_sockaddr);

	wl_list_remove(&ipc_display_destroy.link);
}

void ipc_init(struct sway_server *server) {
	ipc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ipc_socket == -1) {
		sway_abort("Unable to create IPC socket");
	}
	if (fcntl(ipc_socket, F_SETFD, FD_CLOEXEC) == -1) {
		sway_abort("Unable to set CLOEXEC on IPC socket");
	}
	if (fcntl(ipc_socket, F_SETFL, O_NONBLOCK) == -1) {
		sway_abort("Unable to set NONBLOCK on IPC socket");
	}

	ipc_sockaddr = ipc_user_sockaddr();

	// We want to use socket name set by user, not existing socket from another sway instance.
	if (getenv("SWAYSOCK") != NULL && access(getenv("SWAYSOCK"), F_OK) == -1) {
		strncpy(ipc_sockaddr->sun_path, getenv("SWAYSOCK"), sizeof(ipc_sockaddr->sun_path) - 1);
		ipc_sockaddr->sun_path[sizeof(ipc_sockaddr->sun_path) - 1] = 0;
	}

	unlink(ipc_sockaddr->sun_path);
	if (bind(ipc_socket, (struct sockaddr *)ipc_sockaddr, sizeof(*ipc_sockaddr)) == -1) {
		sway_abort("Unable to bind IPC socket");
	}

	if (listen(ipc_socket, 3) == -1) {
		sway_abort("Unable to listen on IPC socket");
	}

	// Set i3 IPC socket path so that i3-msg works out of the box
	setenv("I3SOCK", ipc_sockaddr->sun_path, 1);
	setenv("SWAYSOCK", ipc_sockaddr->sun_path, 1);

	ipc_client_list = create_list();

	ipc_display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(server->wl_display, &ipc_display_destroy);

	ipc_event_source = wl_event_loop_add_fd(server->wl_event_loop, ipc_socket,
			WL_EVENT_READABLE, ipc_handle_connection, server);
}

struct sockaddr_un *ipc_user_sockaddr(void) {
	struct sockaddr_un *ipc_sockaddr = malloc(sizeof(struct sockaddr_un));
	if (ipc_sockaddr == NULL) {
		sway_abort("Can't allocate ipc_sockaddr");
	}

	ipc_sockaddr->sun_family = AF_UNIX;
	int path_size = sizeof(ipc_sockaddr->sun_path);

	// Env var typically set by logind, e.g. "/run/user/<user-id>"
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (!dir) {
		dir = "/tmp";
	}
	if (path_size <= snprintf(ipc_sockaddr->sun_path, path_size,
			"%s/sway-ipc.%u.%i.sock", dir, getuid(), getpid())) {
		sway_abort("Socket path won't fit into ipc_sockaddr->sun_path");
	}

	return ipc_sockaddr;
}

int ipc_handle_connection(int fd, uint32_t mask, void *data) {
	(void) fd;
	struct sway_server *server = data;
	sway_log(SWAY_DEBUG, "Event on IPC listening socket");
	assert(mask == WL_EVENT_READABLE);

	int client_fd = accept(ipc_socket, NULL, NULL);
	if (client_fd == -1) {
		sway_log_errno(SWAY_ERROR, "Unable to accept IPC client connection");
		return 0;
	}

	int flags;
	if ((flags = fcntl(client_fd, F_GETFD)) == -1
			|| fcntl(client_fd, F_SETFD, flags|FD_CLOEXEC) == -1) {
		sway_log_errno(SWAY_ERROR, "Unable to set CLOEXEC on IPC client socket");
		close(client_fd);
		return 0;
	}
	if ((flags = fcntl(client_fd, F_GETFL)) == -1
			|| fcntl(client_fd, F_SETFL, flags|O_NONBLOCK) == -1) {
		sway_log_errno(SWAY_ERROR, "Unable to set NONBLOCK on IPC client socket");
		close(client_fd);
		return 0;
	}

	struct ipc_client *client = malloc(sizeof(struct ipc_client));
	if (!client) {
		sway_log(SWAY_ERROR, "Unable to allocate ipc client");
		close(client_fd);
		return 0;
	}
	client->server = server;
	client->pending_length = 0;
	client->fd = client_fd;
	client->subscribed_events = 0;
	client->event_source = wl_event_loop_add_fd(server->wl_event_loop,
			client_fd, WL_EVENT_READABLE, ipc_client_handle_readable, client);
	client->writable_event_source = NULL;

	client->read_buffer_size = 128;
	client->read_buffer_len = 0;
	client->read_buffer = malloc(client->read_buffer_size);
	if (!client->read_buffer) {
		sway_log(SWAY_ERROR, "Unable to allocate ipc client read buffer");
		free(client);
		close(client_fd);
		return 0;
	}

	client->write_buffer_size = 128;
	client->write_buffer_len = 0;
	client->write_buffer = malloc(client->write_buffer_size);
	if (!client->write_buffer) {
		sway_log(SWAY_ERROR, "Unable to allocate ipc client write buffer");
		free(client->read_buffer);
		free(client);
		close(client_fd);
		return 0;
	}

	sway_log(SWAY_DEBUG, "New client: fd %d", client_fd);
	list_add(ipc_client_list, client);
	return 0;
}

int ipc_client_handle_readable(int client_fd, uint32_t mask, void *data) {
	struct ipc_client *client = data;

	if (mask & WL_EVENT_ERROR) {
		sway_log(SWAY_ERROR, "IPC Client socket error, removing client");
		ipc_client_disconnect(client);
		return 0;
	}

	if (mask & WL_EVENT_HANGUP) {
		sway_log(SWAY_DEBUG, "Client %d hung up", client->fd);
		ipc_client_disconnect(client);
		return 0;
	}

	sway_log(SWAY_DEBUG, "Client %d readable", client->fd);

	int read_available;
	if (ioctl(client_fd, FIONREAD, &read_available) == -1) {
		sway_log_errno(SWAY_INFO, "Unable to read IPC socket buffer size");
		ipc_client_disconnect(client);
		return 0;
	}
	if ((size_t)read_available + 1 > SIZE_MAX - client->read_buffer_len) {
		sway_log_errno(SWAY_INFO, "Receiving too much data from IPC client");
		ipc_client_disconnect(client);
		return 0;
	}

	if (client->read_buffer_len + read_available >= client->read_buffer_size) {
		size_t new_size = client->read_buffer_len + read_available + 1;
		char *new_buffer = realloc(client->read_buffer, new_size);
		if (new_buffer == NULL) {
			sway_log_errno(SWAY_INFO, "Unable to increase read buffer for IPC client");
			ipc_client_disconnect(client);
			return 0;
		}
		client->read_buffer = new_buffer;
		client->read_buffer_size = new_size;
	}

	ssize_t received = recv(client_fd, client->read_buffer + client->read_buffer_len, read_available, 0);
	if (received == -1) {
		sway_log_errno(SWAY_INFO, "Unable to receive header from IPC client");
		ipc_client_disconnect(client);
		return 0;
	}
	client->read_buffer_len += received;

	if (client->pending_length == 0) {
		if (client->read_buffer_len < (size_t) IPC_HEADER_SIZE) {
			return 0;
		}

		if (memcmp(client->read_buffer, ipc_magic, sizeof(ipc_magic)) != 0) {
			sway_log(SWAY_DEBUG, "IPC header check failed");
			ipc_client_disconnect(client);
			return 0;
		}

		memcpy(&client->pending_length,
				client->read_buffer + sizeof(ipc_magic), sizeof(uint32_t));
		memcpy(&client->pending_type,
				client->read_buffer + sizeof(ipc_magic) + sizeof(uint32_t), sizeof(uint32_t));
		if (client->pending_length > IPC_MAX_SIZE) {
			sway_log_errno(SWAY_INFO, "Receiving too much payload from IPC client");
			ipc_client_disconnect(client);
			return 0;
		}
	}

	if (client->read_buffer_len >= IPC_HEADER_SIZE + client->pending_length) {
		uint32_t pending_length = client->pending_length;
		enum ipc_command_type pending_type = client->pending_type;
		char c = client->read_buffer[IPC_HEADER_SIZE + client->pending_length];
		client->read_buffer[IPC_HEADER_SIZE + client->pending_length] = '\0';
		ipc_client_handle_command(client, pending_length, pending_type);
		// Reset values.
		client->read_buffer[IPC_HEADER_SIZE + client->pending_length] = c;
		memmove(client->read_buffer, client->read_buffer + IPC_HEADER_SIZE + client->pending_length,
				client->read_buffer_len - IPC_HEADER_SIZE - client->pending_length);
		client->read_buffer_len -= IPC_HEADER_SIZE + client->pending_length;
		client->pending_length = 0;
	}
	return 0;
}

static bool ipc_has_event_listeners(enum ipc_command_type event) {
	for (int i = 0; i < ipc_client_list->length; i++) {
		struct ipc_client *client = ipc_client_list->items[i];
		if ((client->subscribed_events & event_mask(event)) != 0) {
			return true;
		}
	}
	return false;
}

static void ipc_send_event(const char *json_string, enum ipc_command_type event) {
	struct ipc_client *client;
	for (int i = 0; i < ipc_client_list->length; i++) {
		client = ipc_client_list->items[i];
		if ((client->subscribed_events & event_mask(event)) == 0) {
			continue;
		}
		if (!ipc_send_reply(client, event, json_string,
				(uint32_t)strlen(json_string))) {
			sway_log_errno(SWAY_INFO, "Unable to send reply to IPC client");
			/* ipc_send_reply destroys client on error, which also
			 * removes it from the list, so we need to process
			 * current index again */
			i--;
		}
	}
}

void ipc_event_workspace(struct sway_workspace *old,
		struct sway_workspace *new, const char *change) {
	if (!ipc_has_event_listeners(IPC_EVENT_WORKSPACE)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending workspace::%s event", change);
	json_object *obj = json_object_new_object();
	json_object_object_add(obj, "change", json_object_new_string(change));
	if (old) {
		json_object_object_add(obj, "old",
				ipc_json_describe_node_recursive(&old->node));
	} else {
		json_object_object_add(obj, "old", NULL);
	}

	if (new) {
		json_object_object_add(obj, "current",
				ipc_json_describe_node_recursive(&new->node));
	} else {
		json_object_object_add(obj, "current", NULL);
	}

	const char *json_string = json_object_to_json_string(obj);
	ipc_send_event(json_string, IPC_EVENT_WORKSPACE);
	json_object_put(obj);
}

void ipc_event_window(struct sway_container *window, const char *change) {
	if (!ipc_has_event_listeners(IPC_EVENT_WINDOW)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending window::%s event", change);
	json_object *obj = json_object_new_object();
	json_object_object_add(obj, "change", json_object_new_string(change));
	json_object_object_add(obj, "container",
			ipc_json_describe_node_recursive(&window->node));

	const char *json_string = json_object_to_json_string(obj);
	ipc_send_event(json_string, IPC_EVENT_WINDOW);
	json_object_put(obj);
}

void ipc_event_barconfig_update(struct bar_config *bar) {
	if (!ipc_has_event_listeners(IPC_EVENT_BARCONFIG_UPDATE)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending barconfig_update event");
	json_object *json = ipc_json_describe_bar_config(bar);

	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_BARCONFIG_UPDATE);
	json_object_put(json);
}

void ipc_event_bar_state_update(struct bar_config *bar) {
	if (!ipc_has_event_listeners(IPC_EVENT_BAR_STATE_UPDATE)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending bar_state_update event");

	json_object *json = json_object_new_object();
	json_object_object_add(json, "id", json_object_new_string(bar->id));
	json_object_object_add(json, "visible_by_modifier",
			json_object_new_boolean(bar->visible_by_modifier));

	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_BAR_STATE_UPDATE);
	json_object_put(json);
}

void ipc_event_mode(const char *mode, bool pango) {
	if (!ipc_has_event_listeners(IPC_EVENT_MODE)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending mode::%s event", mode);
	json_object *obj = json_object_new_object();
	json_object_object_add(obj, "change", json_object_new_string(mode));
	json_object_object_add(obj, "pango_markup",
			json_object_new_boolean(pango));

	const char *json_string = json_object_to_json_string(obj);
	ipc_send_event(json_string, IPC_EVENT_MODE);
	json_object_put(obj);
}

void ipc_event_shutdown(const char *reason) {
	if (!ipc_has_event_listeners(IPC_EVENT_SHUTDOWN)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending shutdown::%s event", reason);

	json_object *json = json_object_new_object();
	json_object_object_add(json, "change", json_object_new_string(reason));

	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_SHUTDOWN);
	json_object_put(json);
}

void ipc_event_binding(struct sway_binding *binding) {
	if (!ipc_has_event_listeners(IPC_EVENT_BINDING)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending binding event");

	json_object *json_binding = json_object_new_object();
	json_object_object_add(json_binding, "command", json_object_new_string(binding->command));

	const char *names[10];
	int len = get_modifier_names(names, binding->modifiers);
	json_object *modifiers = json_object_new_array();
	for (int i = 0; i < len; ++i) {
		json_object_array_add(modifiers, json_object_new_string(names[i]));
	}
	json_object_object_add(json_binding, "event_state_mask", modifiers);

	json_object *input_codes = json_object_new_array();
	int input_code = 0;
	json_object *symbols = json_object_new_array();
	json_object *symbol = NULL;

	switch (binding->type) {
	case BINDING_KEYCODE:; // bindcode: populate input_codes
		uint32_t keycode;
		for (int i = 0; i < binding->keys->length; ++i) {
			keycode = *(uint32_t *)binding->keys->items[i];
			json_object_array_add(input_codes, json_object_new_int(keycode));
			if (i == 0) {
				input_code = keycode;
			}
		}
		break;

	case BINDING_KEYSYM:
	case BINDING_MOUSESYM:
	case BINDING_MOUSECODE:; // bindsym/mouse: populate symbols
		uint32_t keysym;
		char buffer[64];
		for (int i = 0; i < binding->keys->length; ++i) {
			keysym = *(uint32_t *)binding->keys->items[i];
			if (keysym >= BTN_LEFT && keysym <= BTN_LEFT + 8) {
				snprintf(buffer, 64, "button%u", keysym - BTN_LEFT + 1);
			} else if (xkb_keysym_get_name(keysym, buffer, 64) < 0) {
				continue;
			}

			json_object *str = json_object_new_string(buffer);
			if (i == 0) {
				// str is owned by both symbol and symbols. Make sure
				// to bump the ref count.
				json_object_array_add(symbols, json_object_get(str));
				symbol = str;
			} else {
				json_object_array_add(symbols, str);
			}
		}
		break;

	default:
		sway_log(SWAY_DEBUG, "Unsupported ipc binding event");
		json_object_put(input_codes);
		json_object_put(symbols);
		json_object_put(json_binding);
		return; // do not send any event
	}

	json_object_object_add(json_binding, "input_codes", input_codes);
	json_object_object_add(json_binding, "input_code", json_object_new_int(input_code));
	json_object_object_add(json_binding, "symbols", symbols);
	json_object_object_add(json_binding, "symbol", symbol);

	bool mouse = binding->type == BINDING_MOUSECODE ||
		binding->type == BINDING_MOUSESYM;
	json_object_object_add(json_binding, "input_type", mouse
			? json_object_new_string("mouse")
			: json_object_new_string("keyboard"));

	json_object *json = json_object_new_object();
	json_object_object_add(json, "change", json_object_new_string("run"));
	json_object_object_add(json, "binding", json_binding);
	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_BINDING);
	json_object_put(json);
}

static void ipc_event_tick(const char *payload) {
	if (!ipc_has_event_listeners(IPC_EVENT_TICK)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending tick event");

	json_object *json = json_object_new_object();
	json_object_object_add(json, "first", json_object_new_boolean(false));
	json_object_object_add(json, "payload", json_object_new_string(payload));

	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_TICK);
	json_object_put(json);
}

void ipc_event_input(const char *change, struct sway_input_device *device) {
	if (!ipc_has_event_listeners(IPC_EVENT_INPUT)) {
		return;
	}
	sway_log(SWAY_DEBUG, "Sending input event");

	json_object *json = json_object_new_object();
	json_object_object_add(json, "change", json_object_new_string(change));
	json_object_object_add(json, "input", ipc_json_describe_input(device));

	const char *json_string = json_object_to_json_string(json);
	ipc_send_event(json_string, IPC_EVENT_INPUT);
	json_object_put(json);
}

int ipc_client_handle_writable(int client_fd, uint32_t mask, void *data) {
	struct ipc_client *client = data;

	if (mask & WL_EVENT_ERROR) {
		sway_log(SWAY_ERROR, "IPC Client socket error, removing client");
		ipc_client_disconnect(client);
		return 0;
	}

	if (mask & WL_EVENT_HANGUP) {
		sway_log(SWAY_DEBUG, "Client %d hung up", client->fd);
		ipc_client_disconnect(client);
		return 0;
	}

	if (client->write_buffer_len <= 0) {
		return 0;
	}

	sway_log(SWAY_DEBUG, "Client %d writable", client->fd);

	ssize_t written = write(client->fd, client->write_buffer, client->write_buffer_len);

	if (written == -1 && errno == EAGAIN) {
		return 0;
	} else if (written == -1) {
		sway_log_errno(SWAY_INFO, "Unable to send data from queue to IPC client");
		ipc_client_disconnect(client);
		return 0;
	}

	memmove(client->write_buffer, client->write_buffer + written, client->write_buffer_len - written);
	client->write_buffer_len -= written;

	if (client->write_buffer_len == 0 && client->writable_event_source) {
		wl_event_source_remove(client->writable_event_source);
		client->writable_event_source = NULL;
	}

	return 0;
}

void ipc_client_disconnect(struct ipc_client *client) {
	if (!sway_assert(client != NULL, "client != NULL")) {
		return;
	}

	shutdown(client->fd, SHUT_RDWR);

	sway_log(SWAY_INFO, "IPC Client %d disconnected", client->fd);
	wl_event_source_remove(client->event_source);
	if (client->writable_event_source) {
		wl_event_source_remove(client->writable_event_source);
	}
	int i = 0;
	while (i < ipc_client_list->length && ipc_client_list->items[i] != client) {
		i++;
	}
	list_del(ipc_client_list, i);
	free(client->read_buffer);
	free(client->write_buffer);
	close(client->fd);
	free(client);
}

static void ipc_get_workspaces_callback(struct sway_workspace *workspace,
		void *data) {
	json_object *workspace_json = ipc_json_describe_node(&workspace->node);
	// override the default focused indicator because
	// it's set differently for the get_workspaces reply
	struct sway_seat *seat = input_manager_get_default_seat();
	struct sway_workspace *focused_ws = seat_get_focused_workspace(seat);
	bool focused = workspace == focused_ws;
	json_object_object_del(workspace_json, "focused");
	json_object_object_add(workspace_json, "focused",
			json_object_new_boolean(focused));
	json_object_array_add((json_object *)data, workspace_json);

	focused_ws = output_get_active_workspace(workspace->output);
	bool visible = workspace == focused_ws;
	json_object_object_add(workspace_json, "visible",
			json_object_new_boolean(visible));
}

static void ipc_get_marks_callback(struct sway_container *con, void *data) {
	json_object *marks = (json_object *)data;
	for (int i = 0; i < con->marks->length; ++i) {
		char *mark = (char *)con->marks->items[i];
		json_object_array_add(marks, json_object_new_string(mark));
	}
}

void ipc_client_handle_command(struct ipc_client *client, uint32_t payload_length,
		enum ipc_command_type payload_type) {
	if (!sway_assert(client != NULL, "client != NULL")) {
		return;
	}

	char *buf = client->read_buffer + IPC_HEADER_SIZE;

	switch (payload_type) {
	case IPC_COMMAND:
	{
		char *line = strtok(buf, "\n");
		while (line) {
			size_t line_length = strlen(line);
			if (line + line_length >= buf + payload_length) {
				break;
			}
			line[line_length] = ';';
			line = strtok(NULL, "\n");
		}

		list_t *res_list = execute_command(buf, NULL, NULL);
		transaction_commit_dirty();
		char *json = cmd_results_to_json(res_list);
		int length = strlen(json);
		ipc_send_reply(client, payload_type, json, (uint32_t)length);
		free(json);
		while (res_list->length) {
			struct cmd_results *results = res_list->items[0];
			free_cmd_results(results);
			list_del(res_list, 0);
		}
		list_free(res_list);
		break;
	}

	case IPC_SEND_TICK:
	{
		ipc_event_tick(buf);
		ipc_send_reply(client, payload_type, "{\"success\": true}", 17);
		break;
	}

	case IPC_GET_OUTPUTS:
	{
		json_object *outputs = json_object_new_array();
		for (int i = 0; i < root->outputs->length; ++i) {
			struct sway_output *output = root->outputs->items[i];
			json_object *output_json = ipc_json_describe_node(&output->node);

			// override the default focused indicator because it's set
			// differently for the get_outputs reply
			struct sway_seat *seat = input_manager_get_default_seat();
			struct sway_workspace *focused_ws =
				seat_get_focused_workspace(seat);
			bool focused = focused_ws && output == focused_ws->output;
			json_object_object_del(output_json, "focused");
			json_object_object_add(output_json, "focused",
				json_object_new_boolean(focused));

			const char *subpixel = sway_wl_output_subpixel_to_string(output->wlr_output->subpixel);
			json_object_object_add(output_json, "subpixel_hinting", json_object_new_string(subpixel));
			json_object_array_add(outputs, output_json);
		}
		struct sway_output *output;
		wl_list_for_each(output, &root->all_outputs, link) {
			if (!output->enabled && output != root->noop_output) {
				json_object_array_add(outputs,
						ipc_json_describe_disabled_output(output));
			}
		}
		const char *json_string = json_object_to_json_string(outputs);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(outputs); // free
		break;
	}

	case IPC_GET_WORKSPACES:
	{
		json_object *workspaces = json_object_new_array();
		root_for_each_workspace(ipc_get_workspaces_callback, workspaces);
		const char *json_string = json_object_to_json_string(workspaces);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(workspaces); // free
		break;
	}

	case IPC_SUBSCRIBE:
	{
		// TODO: Check if they're permitted to use these events
		struct json_object *request = json_tokener_parse(buf);
		if (request == NULL || !json_object_is_type(request, json_type_array)) {
			const char msg[] = "{\"success\": false}";
			ipc_send_reply(client, payload_type, msg, strlen(msg));
			sway_log(SWAY_INFO, "Failed to parse subscribe request");
			break;
		}

		bool is_tick = false;
		// parse requested event types
		for (size_t i = 0; i < json_object_array_length(request); i++) {
			const char *event_type = json_object_get_string(json_object_array_get_idx(request, i));
			if (strcmp(event_type, "workspace") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_WORKSPACE);
			} else if (strcmp(event_type, "barconfig_update") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_BARCONFIG_UPDATE);
			} else if (strcmp(event_type, "bar_state_update") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_BAR_STATE_UPDATE);
			} else if (strcmp(event_type, "mode") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_MODE);
			} else if (strcmp(event_type, "shutdown") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_SHUTDOWN);
			} else if (strcmp(event_type, "window") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_WINDOW);
			} else if (strcmp(event_type, "binding") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_BINDING);
			} else if (strcmp(event_type, "tick") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_TICK);
				is_tick = true;
			} else if (strcmp(event_type, "input") == 0) {
				client->subscribed_events |= event_mask(IPC_EVENT_INPUT);
			} else {
				const char msg[] = "{\"success\": false}";
				ipc_send_reply(client, payload_type, msg, strlen(msg));
				json_object_put(request);
				sway_log(SWAY_INFO, "Unsupported event type in subscribe request");
				return;
			}
		}

		json_object_put(request);
		const char msg[] = "{\"success\": true}";
		ipc_send_reply(client, payload_type, msg, strlen(msg));
		if (is_tick) {
			const char tickmsg[] = "{\"first\": true, \"payload\": \"\"}";
			ipc_send_reply(client, IPC_EVENT_TICK, tickmsg,
				strlen(tickmsg));
		}
		break;
	}

	case IPC_GET_INPUTS:
	{
		json_object *inputs = json_object_new_array();
		struct sway_input_device *device = NULL;
		wl_list_for_each(device, &server.input->devices, link) {
			json_object_array_add(inputs, ipc_json_describe_input(device));
		}
		const char *json_string = json_object_to_json_string(inputs);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(inputs); // free
		break;
	}

	case IPC_GET_SEATS:
	{
		json_object *seats = json_object_new_array();
		struct sway_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			json_object_array_add(seats, ipc_json_describe_seat(seat));
		}
		const char *json_string = json_object_to_json_string(seats);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(seats); // free
		break;
	}

	case IPC_GET_TREE:
	{
		json_object *tree = ipc_json_describe_node_recursive(&root->node);
		const char *json_string = json_object_to_json_string(tree);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(tree);
		break;
	}

	case IPC_GET_MARKS:
	{
		json_object *marks = json_object_new_array();
		root_for_each_container(ipc_get_marks_callback, marks);
		const char *json_string = json_object_to_json_string(marks);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(marks);
		break;
	}

	case IPC_GET_VERSION:
	{
		json_object *version = ipc_json_get_version();
		const char *json_string = json_object_to_json_string(version);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(version); // free
		break;
	}

	case IPC_GET_BAR_CONFIG:
	{
		if (!buf[0]) {
			// Send list of configured bar IDs
			json_object *bars = json_object_new_array();
			for (int i = 0; i < config->bars->length; ++i) {
				struct bar_config *bar = config->bars->items[i];
				json_object_array_add(bars, json_object_new_string(bar->id));
			}
			const char *json_string = json_object_to_json_string(bars);
			ipc_send_reply(client, payload_type, json_string,
				(uint32_t)strlen(json_string));
			json_object_put(bars); // free
		} else {
			// Send particular bar's details
			struct bar_config *bar = NULL;
			for (int i = 0; i < config->bars->length; ++i) {
				bar = config->bars->items[i];
				if (strcmp(buf, bar->id) == 0) {
					break;
				}
				bar = NULL;
			}
			if (!bar) {
				const char *error = "{ \"success\": false, \"error\": \"No bar with that ID\" }";
				ipc_send_reply(client, payload_type, error,
					(uint32_t)strlen(error));
				break;
			}
			json_object *json = ipc_json_describe_bar_config(bar);
			const char *json_string = json_object_to_json_string(json);
			ipc_send_reply(client, payload_type, json_string,
				(uint32_t)strlen(json_string));
			json_object_put(json); // free
		}
		break;
	}

	case IPC_GET_BINDING_MODES:
	{
		json_object *modes = json_object_new_array();
		for (int i = 0; i < config->modes->length; i++) {
			struct sway_mode *mode = config->modes->items[i];
			json_object_array_add(modes, json_object_new_string(mode->name));
		}
		const char *json_string = json_object_to_json_string(modes);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(modes); // free
		break;
	}

	case IPC_GET_BINDING_STATE:
	{
		json_object *current_mode = ipc_json_get_binding_mode();
		const char *json_string = json_object_to_json_string(current_mode);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(current_mode); // free
		break;
	}

	case IPC_GET_CONFIG:
	{
		json_object *json = json_object_new_object();
		json_object_object_add(json, "config", json_object_new_string(config->current_config));
		const char *json_string = json_object_to_json_string(json);
		ipc_send_reply(client, payload_type, json_string,
			(uint32_t)strlen(json_string));
		json_object_put(json); // free
		break;
	}

	case IPC_SYNC:
	{
		// It was decided sway will not support this, just return success:false
		const char msg[] = "{\"success\": false}";
		ipc_send_reply(client, payload_type, msg, strlen(msg));
		break;
	}

	default:
		sway_log(SWAY_INFO, "Unknown IPC command type %x", payload_type);
		break;
	}
}

bool ipc_send_reply(struct ipc_client *client, enum ipc_command_type payload_type,
		const char *payload, uint32_t payload_length) {
	assert(payload);

	char data[IPC_HEADER_SIZE];

	memcpy(data, ipc_magic, sizeof(ipc_magic));
	memcpy(data + sizeof(ipc_magic), &payload_length, sizeof(payload_length));
	memcpy(data + sizeof(ipc_magic) + sizeof(payload_length), &payload_type, sizeof(payload_type));

	while (client->write_buffer_len + IPC_HEADER_SIZE + payload_length >=
				 client->write_buffer_size) {
		client->write_buffer_size *= 2;
	}

	if (client->write_buffer_size > IPC_MAX_SIZE) {
		sway_log(SWAY_ERROR, "Client write buffer too big (%zu), disconnecting client",
				client->write_buffer_size);
		ipc_client_disconnect(client);
		return false;
	}

	char *new_buffer = realloc(client->write_buffer, client->write_buffer_size);
	if (!new_buffer) {
		sway_log(SWAY_ERROR, "Unable to reallocate ipc client write buffer");
		ipc_client_disconnect(client);
		return false;
	}
	client->write_buffer = new_buffer;

	memcpy(client->write_buffer + client->write_buffer_len, data, IPC_HEADER_SIZE);
	client->write_buffer_len += IPC_HEADER_SIZE;
	memcpy(client->write_buffer + client->write_buffer_len, payload, payload_length);
	client->write_buffer_len += payload_length;

	if (!client->writable_event_source) {
		client->writable_event_source = wl_event_loop_add_fd(
				server.wl_event_loop, client->fd, WL_EVENT_WRITABLE,
				ipc_client_handle_writable, client);
	}

	sway_log(SWAY_DEBUG, "Added IPC reply of type 0x%x to client %d queue: %s",
		payload_type, client->fd, payload);
	return true;
}
