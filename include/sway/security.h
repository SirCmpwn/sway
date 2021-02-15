#pragma once

enum security_perm {
	PRIV_LAYER_SHELL,
	PRIV_OUTPUT_MANAGER,
	PRIV_INPUT_METHOD,
	PRIV_TEXT_INPUT,
	PRIV_FOREIGN_TOPLEVEL_MANAGER,
	PRIV_DMABUF_MANAGER,
	PRIV_SCREENCOPY_MANAGER,
	PRIV_DATA_CONTROL_MANAGER,
	PRIV_GAMMA_CONTROL_MANAGER,
	PRIV_INPUT_INHIBIT,
	PRIV_INPUT_KEYBOARD_SHORTCUTS_INHIBIT,
	PRIV_INPUT_VIRTUAL_KEYBOARD,
	PRIV_INPUT_VIRTUAL_POINTER,
	PRIV_OUTPUT_POWER_MANAGER,
	PRIV_LAST, /* not an actual permission */
};

typedef uint32_t security_perm_mask_t;

bool security_global_filter(const struct wl_client *client, const struct wl_global *global, void *data);

/**
 * Get permission value associated with the given name, or PRIV_LAST if there is none
 */
enum security_perm security_get_perm_by_name(const char *name);
