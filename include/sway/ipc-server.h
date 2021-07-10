#ifndef _SWAY_IPC_SERVER_H
#define _SWAY_IPC_SERVER_H
#include <sys/socket.h>
#include "sway/config.h"
#include "sway/desktop/idle_inhibit_v1.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/tree/container.h"
#include "ipc.h"

struct sway_server;

void ipc_init(struct sway_server *server);

struct sockaddr_un *ipc_user_sockaddr(void);

void ipc_event_workspace(struct sway_workspace *old,
		struct sway_workspace *new, const char *change);
void ipc_event_window(struct sway_container *window, const char *change);
void ipc_event_barconfig_update(struct bar_config *bar);
void ipc_event_bar_state_update(struct bar_config *bar);
void ipc_event_mode(const char *mode, bool pango);
void ipc_event_shutdown(const char *reason);
void ipc_event_binding(struct sway_binding *binding);
void ipc_event_input(const char *change, struct sway_input_device *device);
void ipc_event_idle_inhibitor(struct sway_idle_inhibitor_v1 *inhibitor, const char *change);
void ipc_event_keyboard_shortcuts_inhibitor(
		struct sway_keyboard_shortcuts_inhibitor *inhibitor,
		const char *change);

#endif
