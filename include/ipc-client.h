#ifndef _SWAY_IPC_CLIENT_H
#define _SWAY_IPC_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>

#include "ipc.h"

/**
 * IPC response including type of IPC response, size of payload and the json
 * encoded payload string.
 */
struct ipc_response {
	size_t size;
	uint32_t type;
	char *payload;
};

/**
 * Gets the path to the IPC socket from sway.
 */
char *get_socketpath(void);
/**
 * Opens the sway socket.
 */
int ipc_open_socket(const char *socket_path);
/**
 * Issues a single IPC command and returns the buffer. len will be updated with
 * the length of the buffer returned from sway.
 */
char *ipc_single_command(int socketfd, uint32_t type, const char *payload, size_t *len);
/**
 * Receives a single IPC response and returns an ipc_response.
 */
struct ipc_response *ipc_recv_response(int socketfd);
/**
 * Free ipc_response struct
 */
void free_ipc_response(struct ipc_response *response);
/**
 * Sets the receive timeout for the IPC socket
 */
bool ipc_set_recv_timeout(int socketfd, struct timeval tv);

#endif
