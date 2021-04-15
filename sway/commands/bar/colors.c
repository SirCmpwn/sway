#include <string.h>
#include "sway/commands.h"
#include "log.h"
#include "util.h"

// Must be in alphabetical order for bsearch
static const struct cmd_handler bar_colors_handlers[] = {
	{ "active_workspace", bar_colors_cmd_active_workspace },
	{ "background", bar_colors_cmd_background },
	{ "binding_mode", bar_colors_cmd_binding_mode },
	{ "focused_background", bar_colors_cmd_focused_background },
	{ "focused_separator", bar_colors_cmd_focused_separator },
	{ "focused_statusline", bar_colors_cmd_focused_statusline },
	{ "focused_workspace", bar_colors_cmd_focused_workspace },
	{ "inactive_workspace", bar_colors_cmd_inactive_workspace },
	{ "separator", bar_colors_cmd_separator },
	{ "statusline", bar_colors_cmd_statusline },
	{ "urgent_workspace", bar_colors_cmd_urgent_workspace },
};

static char *hex_to_rgba_hex(const char *hex) {
	uint32_t color;
	if (!parse_color(hex, &color)) {
		return NULL;
	}
	char *rgba = malloc(10);
	if (!rgba) {
		return NULL;
	}
	snprintf(rgba, 10, "#%08x", color);
	return rgba;
}

static struct cmd_results *parse_single_color(char **color,
		const char *cmd_name, int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, cmd_name, EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	char *rgba = hex_to_rgba_hex(argv[0]);
	if (!rgba) {
		return cmd_results_new(CMD_INVALID, "Invalid color: %s", argv[0]);
	}

	free(*color);
	*color = rgba;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *parse_three_colors(char ***colors,
		const char *cmd_name, int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, cmd_name, EXPECTED_EQUAL_TO, 3))) {
		return error;
	}

	char *rgba[3] = {0};
	for (int i = 0; i < 3; i++) {
		rgba[i] = hex_to_rgba_hex(argv[i]);
		if (!rgba[i]) {
			return cmd_results_new(CMD_INVALID, "Invalid color: %s", argv[i]);
		}
	}

	for (int i = 0; i < 3; i++) {
		free(*colors[i]);
		*colors[i] = rgba[i];
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *bar_cmd_colors(int argc, char **argv) {
	return config_subcommand(argv, argc, bar_colors_handlers,
			sizeof(bar_colors_handlers));
}

struct cmd_results *bar_colors_cmd_active_workspace(int argc, char **argv) {
	char **colors[3] = {
		&(config->current_bar->colors.active_workspace_border),
		&(config->current_bar->colors.active_workspace_bg),
		&(config->current_bar->colors.active_workspace_text)
	};
	return parse_three_colors(colors, "active_workspace", argc, argv);
}

struct cmd_results *bar_colors_cmd_background(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.background),
			"background", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_background(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.focused_background),
			"focused_background", argc, argv);
}

struct cmd_results *bar_colors_cmd_binding_mode(int argc, char **argv) {
	char **colors[3] = {
		&(config->current_bar->colors.binding_mode_border),
		&(config->current_bar->colors.binding_mode_bg),
		&(config->current_bar->colors.binding_mode_text)
	};
	return parse_three_colors(colors, "binding_mode", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_workspace(int argc, char **argv) {
	char **colors[3] = {
		&(config->current_bar->colors.focused_workspace_border),
		&(config->current_bar->colors.focused_workspace_bg),
		&(config->current_bar->colors.focused_workspace_text)
	};
	return parse_three_colors(colors, "focused_workspace", argc, argv);
}

struct cmd_results *bar_colors_cmd_inactive_workspace(int argc, char **argv) {
	char **colors[3] = {
		&(config->current_bar->colors.inactive_workspace_border),
		&(config->current_bar->colors.inactive_workspace_bg),
		&(config->current_bar->colors.inactive_workspace_text)
	};
	return parse_three_colors(colors, "inactive_workspace", argc, argv);
}

struct cmd_results *bar_colors_cmd_separator(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.separator),
			"separator", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_separator(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.focused_separator),
			"focused_separator", argc, argv);
}

struct cmd_results *bar_colors_cmd_statusline(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.statusline),
			"statusline", argc, argv);
}

struct cmd_results *bar_colors_cmd_focused_statusline(int argc, char **argv) {
	return parse_single_color(&(config->current_bar->colors.focused_statusline),
			"focused_statusline", argc, argv);
}

struct cmd_results *bar_colors_cmd_urgent_workspace(int argc, char **argv) {
	char **colors[3] = {
		&(config->current_bar->colors.urgent_workspace_border),
		&(config->current_bar->colors.urgent_workspace_bg),
		&(config->current_bar->colors.urgent_workspace_text)
	};
	return parse_three_colors(colors, "urgent_workspace", argc, argv);
}
