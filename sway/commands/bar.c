#define _POSIX_C_SOURCE 200809
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/ipc-server.h"
#include "log.h"

// Must be in alphabetical order for bsearch
static const struct cmd_handler bar_handlers[] = {
	{ "bindcode", bar_cmd_bindcode },
	{ "binding_mode_indicator", bar_cmd_binding_mode_indicator },
	{ "bindsym", bar_cmd_bindsym },
	{ "colors", bar_cmd_colors },
	{ "font", bar_cmd_font },
	{ "gaps", bar_cmd_gaps },
	{ "height", bar_cmd_height },
	{ "hidden_state", bar_cmd_hidden_state },
	{ "icon_theme", bar_cmd_icon_theme },
	{ "mode", bar_cmd_mode },
	{ "modifier", bar_cmd_modifier },
	{ "output", bar_cmd_output },
	{ "pango_markup", bar_cmd_pango_markup },
	{ "position", bar_cmd_position },
	{ "separator_symbol", bar_cmd_separator_symbol },
	{ "status_command", bar_cmd_status_command },
	{ "status_edge_padding", bar_cmd_status_edge_padding },
	{ "status_padding", bar_cmd_status_padding },
	{ "strip_workspace_name", bar_cmd_strip_workspace_name },
	{ "strip_workspace_numbers", bar_cmd_strip_workspace_numbers },
	{ "tray_bindcode", bar_cmd_tray_bindcode },
	{ "tray_bindsym", bar_cmd_tray_bindsym },
	{ "tray_output", bar_cmd_tray_output },
	{ "tray_padding", bar_cmd_tray_padding },
	{ "unbindcode", bar_cmd_unbindcode },
	{ "unbindsym", bar_cmd_unbindsym },
	{ "workspace_buttons", bar_cmd_workspace_buttons },
	{ "workspace_min_width", bar_cmd_workspace_min_width },
	{ "wrap_scroll", bar_cmd_wrap_scroll },
};

// Must be in alphabetical order for bsearch
static const struct cmd_handler bar_config_handlers[] = {
	{ "id", bar_cmd_id },
	{ "swaybar_command", bar_cmd_swaybar_command },
};

// Determines whether the subcommand is valid in any bar handler struct
static bool is_subcommand(char *name) {
	return find_handler(name, bar_handlers, sizeof(bar_handlers)) ||
		find_handler(name, bar_config_handlers, sizeof(bar_config_handlers));
}

struct cmd_results *cmd_bar(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "bar", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	char *id = NULL;
	if (strcmp(argv[0], "id") != 0 && is_subcommand(argv[1])) {
		for (int i = 0; i < config->bars->length; ++i) {
			struct bar_config *item = config->bars->items[i];
			if (strcmp(item->id, argv[0]) == 0) {
				sway_log(SWAY_DEBUG, "Selecting bar: %s", argv[0]);
				config->current_bar = item;
				break;
			}
		}
		if (!config->current_bar) {
			id = strdup(argv[0]);
		}
		++argv; --argc;
	} else if (config->reading && !config->current_bar) {
		int len = snprintf(NULL, 0, "bar-%d", config->bars->length) + 1;
		id = malloc(len * sizeof(char));
		if (!id) {
			return cmd_results_new(CMD_FAILURE, "Unable to allocate bar id");
		}
		snprintf(id, len, "bar-%d", config->bars->length);
	} else if (!config->reading && strcmp(argv[0], "mode") != 0 &&
			strcmp(argv[0], "hidden_state") != 0) {
		if (is_subcommand(argv[0])) {
			return cmd_results_new(CMD_INVALID, "No bar defined.");
		} else {
			return cmd_results_new(CMD_INVALID,
					"Unknown/invalid command '%s'", argv[1]);
		}
	}

	if (id) {
		sway_log(SWAY_DEBUG, "Creating bar: %s", id);
		config->current_bar = default_bar_config();
		if (!config->current_bar) {
			free(id);
			return cmd_results_new(CMD_FAILURE, "Unable to allocate bar config");
		}
		config->current_bar->id = id;
	}

	struct cmd_results *res = NULL;
	if (find_handler(argv[0], bar_config_handlers,
				sizeof(bar_config_handlers))) {
		if (config->reading) {
			res = config_subcommand(argv, argc, bar_config_handlers,
					sizeof(bar_config_handlers));
		} else {
			res = cmd_results_new(CMD_INVALID,
					"Can only be used in the config file");
		}
	} else {
		res = config_subcommand(argv, argc, bar_handlers, sizeof(bar_handlers));
	}

	if (res && res->status != CMD_SUCCESS) {
		if (id) {
			free_bar_config(config->current_bar);
			id = NULL;
		}
		return res;
	}

	if (id) {
		list_add(config->bars, config->current_bar);
	}

	if (!config->reading && config->current_bar) {
		ipc_event_barconfig_update(config->current_bar);
		if (id) {
			load_swaybar(config->current_bar);
		}
		config->current_bar = NULL;
	}

	return res;
}
