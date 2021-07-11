#ifndef _SWAY_IPC_H
#define _SWAY_IPC_H

#define event_mask(ev) (1 << (ev & 0x7F))

// maximum size of payload is 4 MB
#define IPC_MAX_SIZE 4e6

enum ipc_command_type {
	// i3 command types - see i3's I3_REPLY_TYPE constants
	IPC_COMMAND = 0,
	IPC_GET_WORKSPACES = 1,
	IPC_SUBSCRIBE = 2,
	IPC_GET_OUTPUTS = 3,
	IPC_GET_TREE = 4,
	IPC_GET_MARKS = 5,
	IPC_GET_BAR_CONFIG = 6,
	IPC_GET_VERSION = 7,
	IPC_GET_BINDING_MODES = 8,
	IPC_GET_CONFIG = 9,
	IPC_SEND_TICK = 10,
	IPC_SYNC = 11,
	IPC_GET_BINDING_STATE = 12,

	// sway-specific command types
	IPC_GET_INPUTS = 100,
	IPC_GET_SEATS = 101,

	// Events sent from sway to clients. Events have the highest bits set.
	IPC_EVENT_WORKSPACE = ((1<<31) | 0),
	IPC_EVENT_OUTPUT = ((1<<31) | 1),
	IPC_EVENT_MODE = ((1<<31) | 2),
	IPC_EVENT_WINDOW = ((1<<31) | 3),
	IPC_EVENT_BARCONFIG_UPDATE = ((1<<31) | 4),
	IPC_EVENT_BINDING = ((1<<31) | 5),
	IPC_EVENT_SHUTDOWN = ((1<<31) | 6),
	IPC_EVENT_TICK = ((1<<31) | 7),

	// sway-specific event types
	IPC_EVENT_BAR_STATE_UPDATE = ((1<<31) | 20),
	IPC_EVENT_INPUT = ((1<<31) | 21),
};

#endif
