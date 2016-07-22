#ifndef _SWAY_IPC_SERVER_H
#define _SWAY_IPC_SERVER_H

#include <wlc/wlc.h>

#include "container.h"
#include "config.h"
#include "ipc.h"

void ipc_init(void);
void ipc_terminate(void);
struct sockaddr_un *ipc_user_sockaddr(void);

void ipc_event_workspace(swayc_t *old, swayc_t *new, const char *change);
void ipc_event_barconfig_update(struct bar_config *bar);
/**
 * Send IPC mode event to all listening clients
 */
void ipc_event_mode(const char *mode);
/**
 * Send IPC window change event
 */
void ipc_event_window(swayc_t *window, const char *change);
/**
 * Sends an IPC modifier event to all listening clients.  The modifier event
 * includes a key 'change' with the value of state and a key 'modifier' with
 * the name of that modifier.
 */
void ipc_event_modifier(uint32_t modifier, const char *state);
/**
 * Send IPC keyboard binding event.
 */
void ipc_event_binding_keyboard(struct sway_binding *sb);
const char *swayc_type_string(enum swayc_types type);

/**
 * Send pixel data to registered clients.
 */
void ipc_get_pixels(wlc_handle output);

#endif
