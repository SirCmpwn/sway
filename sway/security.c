#define _XOPEN_SOURCE 500 // for strdup
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/noop.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include "config.h"
#include "list.h"
#include "log.h"
#include "stringop.h"
#include "sway/client_label.h"
#include "sway/config.h"
#include "sway/desktop/idle_inhibit_v1.h"
#include "sway/input/input-manager.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/security.h"
#include "sway/tree/root.h"

bool security_global_filter(const struct wl_client *client, const struct wl_global *global, void *data) {
	struct sway_server *server = data;
	enum security_perm perm;

	// with no security config, quickly default to allow all
	if (config->security_configs == NULL || config->security_configs->length == 0)
		return true;

	if (global == server->layer_shell->global)
		perm = PRIV_LAYER_SHELL;
	else if (global == server->output_manager_v1->global)
		perm = PRIV_OUTPUT_MANAGER;
	else if (global == server->input_method->global)
		perm = PRIV_INPUT_METHOD;
	else if (global == server->text_input->global)
		perm = PRIV_TEXT_INPUT;
	else if (global == server->foreign_toplevel_manager->global)
		perm = PRIV_FOREIGN_TOPLEVEL_MANAGER;
	else if (global == server->dmabuf_manager->global)
		perm = PRIV_DMABUF_MANAGER;
	else if (global == server->screencopy_manager->global )
		perm = PRIV_SCREENCOPY_MANAGER;
	else if (global == server->data_control_manager->global)
		perm = PRIV_DATA_CONTROL_MANAGER;
	else if (global == server->gamma_control_manager->global)
		perm = PRIV_GAMMA_CONTROL_MANAGER;
	else if (global == server->input->inhibit->global)
		perm = PRIV_INPUT_INHIBIT;
	else if (global == server->input->keyboard_shortcuts_inhibit->global)
		perm = PRIV_INPUT_KEYBOARD_SHORTCUTS_INHIBIT;
	else if (global == server->input->virtual_keyboard->global)
		perm = PRIV_INPUT_VIRTUAL_KEYBOARD;
	else if (global == server->input->virtual_pointer->global)
		perm = PRIV_INPUT_VIRTUAL_POINTER;
	else if (global == server->output_power_manager_v1->global)
		perm = PRIV_OUTPUT_POWER_MANAGER;
	else
		return true;

	security_perm_mask_t priv_bit = 1 << perm;
	const char* label = wl_client_label_get((struct wl_client*)client);
	if (label != NULL) {
		for (int i = 0; i < config->security_configs->length; i++) {
			struct security_config *cfg = config->security_configs->items[i];
			if (lenient_strcmp(cfg->name, label) == 0) {
				return !!(priv_bit & cfg->permitted);
			}
		}
	}

	for (int i = 0; i < config->security_configs->length; i++) {
		struct security_config *cfg = config->security_configs->items[i];
		if (lenient_strcmp(cfg->name, "default") == 0) {
			return !!(priv_bit & cfg->permitted);
		}
	}
	return true;
}

static const char* const perm_names[PRIV_LAST] = {
	"layer_shell",
	"output_manager",
	"input_method",
	"text_input",
	"foreign_toplevel_manager",
	"dmabuf_manager",
	"screencopy_manager",
	"data_control_manager",
	"gamma_control_manager",
	"input_inhibit",
	"input_keyboard_shortcuts_inhibit",
	"input_virtual_keyboard",
	"input_virtual_pointer",
	"output_power_manager",
};

enum security_perm security_get_perm_by_name(const char *name)
{
	int i;
	for(i = 0; i < PRIV_LAST; i++) {
		if (0 == strcmp(name, perm_names[i]))
			return i;
	}
	return PRIV_LAST;
}
