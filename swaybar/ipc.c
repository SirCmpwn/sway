#define _POSIX_C_SOURCE 200809
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <json.h>
#include "swaybar/config.h"
#include "swaybar/ipc.h"
#include "swaybar/status_line.h"
#if HAVE_TRAY
#include "swaybar/tray/tray.h"
#endif
#include "config.h"
#include "ipc-client.h"
#include "list.h"
#include "log.h"
#include "loop.h"
#include "util.h"

void ipc_send_workspace_command(struct swaybar *bar, const char *ws) {
	uint32_t size = strlen("workspace \"\"") + strlen(ws);
	for (size_t i = 0; i < strlen(ws); ++i) {
		if (ws[i] == '"' || ws[i] == '\\') {
			++size;
		}
	}

	char *command = malloc(size + 1);
	if (!command) {
		return;
	}

	strcpy(command, "workspace \"");
	strcpy(&command[size - 1], "\"");
	for (size_t i = 0, d = strlen("workspace \""); i < strlen(ws); ++i) {
		if (ws[i] == '"' || ws[i] == '\\') {
			command[d++] = '\\';
		}
		command[d++] = ws[i];
	}

	free(ipc_single_command(bar->ipc_socketfd, IPC_COMMAND, command, &size));
	free(command);
}

char *parse_font(const char *font) {
	char *new_font = NULL;
	if (strncmp("pango:", font, 6) == 0) {
		font += 6;
	}
	new_font = strdup(font);
	return new_font;
}

static void ipc_parse_colors(
		struct swaybar_config *config, json_object *colors) {
	struct {
		const char *name;
		uint32_t *color;
	} properties[] = {
		{ "background", &config->colors.background },
		{ "statusline", &config->colors.statusline },
		{ "separator", &config->colors.separator },
		{ "focused_background", &config->colors.focused_background },
		{ "focused_statusline", &config->colors.focused_statusline },
		{ "focused_separator", &config->colors.focused_separator },
		{ "focused_workspace_border", &config->colors.focused_workspace.border },
		{ "focused_workspace_bg", &config->colors.focused_workspace.background },
		{ "focused_workspace_text", &config->colors.focused_workspace.text },
		{ "active_workspace_border", &config->colors.active_workspace.border },
		{ "active_workspace_bg", &config->colors.active_workspace.background },
		{ "active_workspace_text", &config->colors.active_workspace.text },
		{ "inactive_workspace_border", &config->colors.inactive_workspace.border },
		{ "inactive_workspace_bg", &config->colors.inactive_workspace.background },
		{ "inactive_workspace_text", &config->colors.inactive_workspace.text },
		{ "urgent_workspace_border", &config->colors.urgent_workspace.border },
		{ "urgent_workspace_bg", &config->colors.urgent_workspace.background },
		{ "urgent_workspace_text", &config->colors.urgent_workspace.text },
		{ "binding_mode_border", &config->colors.binding_mode.border },
		{ "binding_mode_bg", &config->colors.binding_mode.background },
		{ "binding_mode_text", &config->colors.binding_mode.text },
	};

	for (size_t i = 0; i < sizeof(properties) / sizeof(properties[i]); i++) {
		json_object *object;
		if (json_object_object_get_ex(colors, properties[i].name, &object)) {
			const char *hexstring = json_object_get_string(object);
			if (!parse_color(hexstring, properties[i].color)) {
				sway_log(SWAY_ERROR, "Ignoring invalid %s: %s",
						properties[i].name, hexstring);
			}
		}
	}
}

static bool ipc_parse_config(
		struct swaybar_config *config, const char *payload) {
	json_object *bar_config = json_tokener_parse(payload);
	json_object *success;
	if (json_object_object_get_ex(bar_config, "success", &success)
			&& !json_object_get_boolean(success)) {
		sway_log(SWAY_ERROR, "No bar with that ID. Use 'swaymsg -t "
				"get_bar_config' to get the available bar configs.");
		json_object_put(bar_config);
		return false;
	}

	json_object *bar_height = json_object_object_get(bar_config, "bar_height");
	if (bar_height) {
		config->height = json_object_get_int(bar_height);
	}

	json_object *binding_mode_indicator =
		json_object_object_get(bar_config, "binding_mode_indicator");
	if (binding_mode_indicator) {
		config->binding_mode_indicator =
			json_object_get_boolean(binding_mode_indicator);
	}

	json_object *bindings = json_object_object_get(bar_config, "bindings");
	while (config->bindings->length) {
		struct swaybar_binding *binding = config->bindings->items[0];
		list_del(config->bindings, 0);
		free_binding(binding);
	}
	if (bindings) {
		int length = json_object_array_length(bindings);
		for (int i = 0; i < length; ++i) {
			json_object *bindobj = json_object_array_get_idx(bindings, i);
			struct swaybar_binding *binding =
				calloc(1, sizeof(struct swaybar_binding));
			binding->button = json_object_get_int(
					json_object_object_get(bindobj, "event_code"));
			binding->command = strdup(json_object_get_string(
					json_object_object_get(bindobj, "command")));
			binding->release = json_object_get_boolean(
					json_object_object_get(bindobj, "release"));
			list_add(config->bindings, binding);
		}
	}

	json_object *colors = json_object_object_get(bar_config, "colors");
	if (colors) {
		ipc_parse_colors(config, colors);
	}

	json_object *font = json_object_object_get(bar_config, "font");
	if (font) {
		free(config->font);
		config->font = parse_font(json_object_get_string(font));
	}

	json_object *gaps = json_object_object_get(bar_config, "gaps");
	if (gaps) {
		json_object *top = json_object_object_get(gaps, "top");
		if (top) {
			config->gaps.top = json_object_get_int(top);
		}
		json_object *right = json_object_object_get(gaps, "right");
		if (right) {
			config->gaps.right = json_object_get_int(right);
		}
		json_object *bottom = json_object_object_get(gaps, "bottom");
		if (bottom) {
			config->gaps.bottom = json_object_get_int(bottom);
		}
		json_object *left = json_object_object_get(gaps, "left");
		if (left) {
			config->gaps.left = json_object_get_int(left);
		}
	}

	json_object *hidden_state =
		json_object_object_get(bar_config, "hidden_state");
	if (hidden_state) {
		free(config->hidden_state);
		config->hidden_state = strdup(json_object_get_string(hidden_state));
	}

	json_object *mode = json_object_object_get(bar_config, "mode");
	if (mode) {
		free(config->mode);
		config->mode = strdup(json_object_get_string(mode));
	}

	json_object *outputs = json_object_object_get(bar_config, "outputs");
	struct config_output *output, *tmp;
	wl_list_for_each_safe(output, tmp, &config->outputs, link) {
		wl_list_remove(&output->link);
		free(output->name);
		free(output);
	}
	if (outputs) {
		int length = json_object_array_length(outputs);
		for (int i = 0; i < length; ++i) {
			json_object *output = json_object_array_get_idx(outputs, i);
			const char *name = json_object_get_string(output);
			if (strcmp("*", name) == 0) {
				struct config_output *coutput, *tmp;
				wl_list_for_each_safe(coutput, tmp, &config->outputs, link) {
					wl_list_remove(&coutput->link);
					free(coutput->name);
					free(coutput);
				}
				break;
			}
			struct config_output *coutput = calloc(
					1, sizeof(struct config_output));
			coutput->name = strdup(name);
			wl_list_insert(&config->outputs, &coutput->link);
		}
	}

	json_object *pango_markup =
		json_object_object_get(bar_config, "pango_markup");
	if (pango_markup) {
		config->pango_markup = json_object_get_boolean(pango_markup);
	}

	json_object *position = json_object_object_get(bar_config, "position");
	if (position) {
		config->position = parse_position(json_object_get_string(position));
	}

	json_object *separator_symbol =
		json_object_object_get(bar_config, "separator_symbol");
	if (separator_symbol) {
		free(config->sep_symbol);
		config->sep_symbol = strdup(json_object_get_string(separator_symbol));
	}

	json_object *status_command =
		json_object_object_get(bar_config, "status_command");
	if (status_command) {
		const char *command = json_object_get_string(status_command);
		free(config->status_command);
		config->status_command = strdup(command);
	}

	json_object *status_edge_padding =
		json_object_object_get(bar_config, "status_edge_padding");
	if (status_edge_padding) {
		config->status_edge_padding = json_object_get_int(status_edge_padding);
	}

	json_object *status_padding =
		json_object_object_get(bar_config, "status_padding");
	if (status_padding) {
		config->status_padding = json_object_get_int(status_padding);
	}

	json_object *strip_workspace_name =
		json_object_object_get(bar_config, "strip_workspace_name");
	if (strip_workspace_name) {
		config->strip_workspace_name =
			json_object_get_boolean(strip_workspace_name);
	}

	json_object *strip_workspace_numbers =
		json_object_object_get(bar_config, "strip_workspace_numbers");
	if (strip_workspace_numbers) {
		config->strip_workspace_numbers =
			json_object_get_boolean(strip_workspace_numbers);
	}

	json_object *workspace_buttons =
		json_object_object_get(bar_config, "workspace_buttons");
	if (workspace_buttons) {
		config->workspace_buttons = json_object_get_boolean(workspace_buttons);
	}

	json_object *workspace_min_width =
		json_object_object_get(bar_config, "workspace_min_width");
	if (workspace_min_width) {
		config->workspace_min_width = json_object_get_int(workspace_min_width);
	}

	json_object *wrap_scroll = json_object_object_get(bar_config, "wrap_scroll");
	if (wrap_scroll) {
		config->wrap_scroll = json_object_get_boolean(wrap_scroll);
	}
#if HAVE_TRAY
	json_object *tray_outputs, *tray_padding, *tray_bindings, *icon_theme;

	if (config->tray_outputs && config->tray_outputs->length) {
		list_free_items_and_destroy(config->tray_outputs);
	}
	if ((json_object_object_get_ex(bar_config, "tray_outputs", &tray_outputs))) {
		config->tray_outputs = create_list();
		int length = json_object_array_length(tray_outputs);
		for (int i = 0; i < length; ++i) {
			json_object *output= json_object_array_get_idx(tray_outputs, i);
			const char *name = json_object_get_string(output);
			if (strcmp(name, "none") == 0) {
				config->tray_hidden = true;
				list_free_items_and_destroy(config->tray_outputs);
				config->tray_outputs = create_list();
				break;
			}
			list_add(config->tray_outputs, strdup(name));
		}
	}

	if ((json_object_object_get_ex(bar_config, "tray_padding", &tray_padding))) {
		config->tray_padding = json_object_get_int(tray_padding);
	}

	struct tray_binding *tray_bind = NULL, *tmp_tray_bind = NULL;
	wl_list_for_each_safe(tray_bind, tmp_tray_bind, &config->tray_bindings,
			link) {
		wl_list_remove(&tray_bind->link);
		free_tray_binding(tray_bind);
	}
	if ((json_object_object_get_ex(bar_config, "tray_bindings", &tray_bindings))) {
		int length = json_object_array_length(tray_bindings);
		for (int i = 0; i < length; ++i) {
			json_object *bind = json_object_array_get_idx(tray_bindings, i);
			struct tray_binding *binding =
				calloc(1, sizeof(struct tray_binding));
			binding->button = json_object_get_int(
					json_object_object_get(bind, "event_code"));
			binding->command = strdup(json_object_get_string(
					json_object_object_get(bind, "command")));
			wl_list_insert(&config->tray_bindings, &binding->link);
		}
	}

	if ((json_object_object_get_ex(bar_config, "icon_theme", &icon_theme))) {
		config->icon_theme = strdup(json_object_get_string(icon_theme));
	}
#endif

	json_object_put(bar_config);
	return true;
}

bool ipc_get_workspaces(struct swaybar *bar) {
	struct swaybar_output *output;
	wl_list_for_each(output, &bar->outputs, link) {
		free_workspaces(&output->workspaces);
		output->focused = false;
	}
	uint32_t len = 0;
	char *res = ipc_single_command(bar->ipc_socketfd,
			IPC_GET_WORKSPACES, NULL, &len);
	json_object *results = json_tokener_parse(res);
	if (!results) {
		free(res);
		return false;
	}

	bar->visible_by_urgency = false;
	size_t length = json_object_array_length(results);
	json_object *ws_json;
	json_object *num, *name, *visible, *focused, *out, *urgent;
	for (size_t i = 0; i < length; ++i) {
		ws_json = json_object_array_get_idx(results, i);

		json_object_object_get_ex(ws_json, "num", &num);
		json_object_object_get_ex(ws_json, "name", &name);
		json_object_object_get_ex(ws_json, "visible", &visible);
		json_object_object_get_ex(ws_json, "focused", &focused);
		json_object_object_get_ex(ws_json, "output", &out);
		json_object_object_get_ex(ws_json, "urgent", &urgent);

		wl_list_for_each(output, &bar->outputs, link) {
			const char *ws_output = json_object_get_string(out);
			if (ws_output != NULL && strcmp(ws_output, output->name) == 0) {
				struct swaybar_workspace *ws =
					calloc(1, sizeof(struct swaybar_workspace));
				ws->num = json_object_get_int(num);
				ws->name = strdup(json_object_get_string(name));
				ws->label = strdup(ws->name);
				// ws->num will be -1 if workspace name doesn't begin with int.
				if (ws->num != -1) {
					size_t len_offset = snprintf(NULL, 0, "%d", ws->num);
					if (bar->config->strip_workspace_name) {
						free(ws->label);
						ws->label = malloc(len_offset + 1);
						snprintf(ws->label, len_offset + 1, "%d", ws->num);
					} else if (bar->config->strip_workspace_numbers) {
						len_offset += ws->label[len_offset] == ':';
						if (ws->name[len_offset] != '\0') {
							free(ws->label);
							// Strip number prefix [1-?:] using len_offset.
							ws->label = strdup(ws->name + len_offset);
						}
					}
				}
				ws->visible = json_object_get_boolean(visible);
				ws->focused = json_object_get_boolean(focused);
				if (ws->focused) {
					output->focused = true;
				}
				ws->urgent = json_object_get_boolean(urgent);
				if (ws->urgent) {
					bar->visible_by_urgency = true;
				}
				wl_list_insert(output->workspaces.prev, &ws->link);
			}
		}
	}
	json_object_put(results);
	free(res);
	return determine_bar_visibility(bar, false);
}

void ipc_execute_binding(struct swaybar *bar, struct swaybar_binding *bind) {
	sway_log(SWAY_DEBUG, "Executing binding for button %u (release=%d): `%s`",
			bind->button, bind->release, bind->command);
	uint32_t len = strlen(bind->command);
	free(ipc_single_command(bar->ipc_socketfd,
			IPC_COMMAND, bind->command, &len));
}

bool ipc_initialize(struct swaybar *bar) {
	uint32_t len = strlen(bar->id);
	char *res = ipc_single_command(bar->ipc_socketfd,
			IPC_GET_BAR_CONFIG, bar->id, &len);
	if (!ipc_parse_config(bar->config, res)) {
		free(res);
		return false;
	}
	free(res);

	struct swaybar_config *config = bar->config;
	char subscribe[128]; // suitably large buffer
	len = snprintf(subscribe, 128,
			"[ \"barconfig_update\" , \"bar_state_update\" %s %s ]",
			config->binding_mode_indicator ? ", \"mode\"" : "",
			config->workspace_buttons ? ", \"workspace\"" : "");
	free(ipc_single_command(bar->ipc_event_socketfd,
			IPC_SUBSCRIBE, subscribe, &len));
	return true;
}

static bool handle_bar_state_update(struct swaybar *bar, json_object *event) {
	json_object *json_id;
	json_object_object_get_ex(event, "id", &json_id);
	const char *id = json_object_get_string(json_id);
	if (strcmp(id, bar->id) != 0) {
		return false;
	}

	json_object *visible_by_modifier;
	json_object_object_get_ex(event, "visible_by_modifier", &visible_by_modifier);
	bar->visible_by_modifier = json_object_get_boolean(visible_by_modifier);
	if (bar->visible_by_modifier) {
		// If the bar is visible by modifier, clear both visible by mode and
		// urgency as modifier has precedence and the bar should be hidden
		// again when it is no longer visible by modifier.
		bar->visible_by_mode = false;
		bar->visible_by_urgency = false;
	}
	return determine_bar_visibility(bar, false);
}

static bool handle_barconfig_update(struct swaybar *bar, const char *payload,
		json_object *json_config) {
	json_object *json_id = json_object_object_get(json_config, "id");
	const char *id = json_object_get_string(json_id);
	if (strcmp(id, bar->id) != 0) {
		return false;
	}

	struct swaybar_config *newcfg = init_config();
	ipc_parse_config(newcfg, payload);

	struct swaybar_config *oldcfg = bar->config;
	bar->config = newcfg;

	struct swaybar_output *output, *tmp_output;
	wl_list_for_each_safe(output, tmp_output, &bar->outputs, link) {
		bool found = wl_list_empty(&newcfg->outputs);
		struct config_output *coutput;
		wl_list_for_each(coutput, &newcfg->outputs, link) {
			if (strcmp(coutput->name, output->name) == 0 ||
					strcmp(coutput->name, output->identifier) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			destroy_layer_surface(output);
			wl_list_remove(&output->link);
			wl_list_insert(&bar->unused_outputs, &output->link);
		} else if (!oldcfg->font || !newcfg->font ||
				strcmp(oldcfg->font, newcfg->font) != 0) {
			output->height = 0;  // force update height
		}
	}
	wl_list_for_each_safe(output, tmp_output, &bar->unused_outputs, link) {
		bool found = wl_list_empty(&newcfg->outputs);
		struct config_output *coutput;
		wl_list_for_each(coutput, &newcfg->outputs, link) {
			if (strcmp(coutput->name, output->name) == 0 ||
					strcmp(coutput->name, output->identifier) == 0) {
				found = true;
				break;
			}
		}
		if (found) {
			wl_list_remove(&output->link);
			wl_list_insert(&bar->outputs, &output->link);
		}
	}

	if (bar->status && (!newcfg->status_command ||
				strcmp(newcfg->status_command, oldcfg->status_command) != 0)) {
		status_line_free(bar->status);
		bar->status = NULL;
	}
	if (!bar->status && newcfg->status_command) {
		bar->status = status_line_init(newcfg->status_command);
		bar->status->bar = bar;
		loop_add_fd(bar->eventloop, bar->status->read_fd, POLLIN,
				status_in, bar);
	}

#if HAVE_TRAY
	if (oldcfg->tray_hidden && !newcfg->tray_hidden) {
		bar->tray = create_tray(bar);
		loop_add_fd(bar->eventloop, bar->tray->fd, POLLIN, tray_in,
				bar->tray->bus);
	} else if (bar->tray && newcfg->tray_hidden) {
		loop_remove_fd(bar->eventloop, bar->tray->fd);
		destroy_tray(bar->tray);
		bar->tray = NULL;
	}
#endif

	if (newcfg->workspace_buttons) {
		ipc_get_workspaces(bar);
	}

	bool moving_layer = strcmp(oldcfg->mode, newcfg->mode) != 0;

	free_config(oldcfg);
	determine_bar_visibility(bar, moving_layer);
	return true;
}

bool handle_ipc_readable(struct swaybar *bar) {
	struct ipc_response *resp = ipc_recv_response(bar->ipc_event_socketfd);
	if (!resp) {
		return false;
	}

	// The default depth of 32 is too small to represent some nested layouts, but
	// we can't pass INT_MAX here because json-c (as of this writing) prefaults
	// all the memory for its stack.
	json_tokener *tok = json_tokener_new_ex(256);
	if (!tok) {
		sway_log_errno(SWAY_ERROR, "failed to create tokener");
		free_ipc_response(resp);
		return false;
	}

	json_object *result = json_tokener_parse_ex(tok, resp->payload, -1);
	enum json_tokener_error err = json_tokener_get_error(tok);
	json_tokener_free(tok);

	if (err != json_tokener_success) {
		sway_log(SWAY_ERROR, "failed to parse payload as json: %s",
				json_tokener_error_desc(err));
		free_ipc_response(resp);
		return false;
	}

	bool bar_is_dirty = true;
	switch (resp->type) {
	case IPC_EVENT_WORKSPACE:
		bar_is_dirty = ipc_get_workspaces(bar);
		break;
	case IPC_EVENT_MODE: {
		json_object *json_change, *json_pango_markup;
		if (json_object_object_get_ex(result, "change", &json_change)) {
			const char *change = json_object_get_string(json_change);
			free(bar->mode);
			bar->mode = strcmp(change, "default") != 0 ? strdup(change) : NULL;
			bar->visible_by_mode = bar->mode != NULL;
			determine_bar_visibility(bar, false);
		} else {
			sway_log(SWAY_ERROR, "failed to parse response");
			bar_is_dirty = false;
			break;
		}
		if (json_object_object_get_ex(result,
					"pango_markup", &json_pango_markup)) {
			bar->mode_pango_markup = json_object_get_boolean(json_pango_markup);
		}
		break;
	}
	case IPC_EVENT_BARCONFIG_UPDATE:
		bar_is_dirty = handle_barconfig_update(bar, resp->payload, result);
		break;
	case IPC_EVENT_BAR_STATE_UPDATE:
		bar_is_dirty = handle_bar_state_update(bar, result);
		break;
	default:
		bar_is_dirty = false;
		break;
	}
	json_object_put(result);
	free_ipc_response(resp);
	return bar_is_dirty;
}
