#define _POSIX_C_SOURCE 200809
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <json.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/criteria.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/tree/view.h"
#include "stringop.h"
#include "log.h"

// Returns error object, or NULL if check succeeds.
struct cmd_results *checkarg(int argc, const char *name, enum expected_args type, int val) {
	const char *error_name = NULL;
	switch (type) {
	case EXPECTED_AT_LEAST:
		if (argc < val) {
			error_name = "at least ";
		}
		break;
	case EXPECTED_AT_MOST:
		if (argc > val) {
			error_name = "at most ";
		}
		break;
	case EXPECTED_EQUAL_TO:
		if (argc != val) {
			error_name = "";
		}
	}
	return error_name ?
		cmd_results_new(CMD_INVALID, "Invalid %s command "
				"(expected %s%d argument%s, got %d)",
				name, error_name, val, val != 1 ? "s" : "", argc)
		: NULL;
}

/* Keep alphabetized */
static const struct cmd_handler handlers[] = {
	{ "assign", cmd_assign },
	{ "bar", cmd_bar },
	{ "bindcode", cmd_bindcode },
	{ "bindswitch", cmd_bindswitch },
	{ "bindsym", cmd_bindsym },
	{ "client.background", cmd_client_noop },
	{ "client.focused", cmd_client_focused },
	{ "client.focused_inactive", cmd_client_focused_inactive },
	{ "client.placeholder", cmd_client_noop },
	{ "client.unfocused", cmd_client_unfocused },
	{ "client.urgent", cmd_client_urgent },
	{ "default_border", cmd_default_border },
	{ "default_floating_border", cmd_default_floating_border },
	{ "exec", cmd_exec },
	{ "exec_always", cmd_exec_always },
	{ "floating_maximum_size", cmd_floating_maximum_size },
	{ "floating_minimum_size", cmd_floating_minimum_size },
	{ "floating_modifier", cmd_floating_modifier },
	{ "focus", cmd_focus },
	{ "focus_follows_mouse", cmd_focus_follows_mouse },
	{ "focus_on_window_activation", cmd_focus_on_window_activation },
	{ "focus_wrapping", cmd_focus_wrapping },
	{ "font", cmd_font },
	{ "for_window", cmd_for_window },
	{ "force_display_urgency_hint", cmd_force_display_urgency_hint },
	{ "force_focus_wrapping", cmd_force_focus_wrapping },
	{ "fullscreen", cmd_fullscreen },
	{ "gaps", cmd_gaps },
	{ "hide_edge_borders", cmd_hide_edge_borders },
	{ "input", cmd_input },
	{ "mode", cmd_mode },
	{ "mouse_warping", cmd_mouse_warping },
	{ "new_float", cmd_new_float },
	{ "new_window", cmd_new_window },
	{ "no_focus", cmd_no_focus },
	{ "output", cmd_output },
	{ "popup_during_fullscreen", cmd_popup_during_fullscreen },
	{ "seat", cmd_seat },
	{ "set", cmd_set },
	{ "show_marks", cmd_show_marks },
	{ "smart_borders", cmd_smart_borders },
	{ "smart_gaps", cmd_smart_gaps },
	{ "tiling_drag", cmd_tiling_drag },
	{ "tiling_drag_threshold", cmd_tiling_drag_threshold },
	{ "title_align", cmd_title_align },
	{ "titlebar_border_thickness", cmd_titlebar_border_thickness },
	{ "titlebar_padding", cmd_titlebar_padding },
	{ "unbindcode", cmd_unbindcode },
	{ "unbindswitch", cmd_unbindswitch },
	{ "unbindsym", cmd_unbindsym },
	{ "workspace", cmd_workspace },
	{ "workspace_auto_back_and_forth", cmd_ws_auto_back_and_forth },
	{ "xwayland", cmd_xwayland },
};

/* Config-time only commands. Keep alphabetized */
static const struct cmd_handler config_handlers[] = {
	{ "default_orientation", cmd_default_orientation },
	{ "include", cmd_include },
	{ "swaybg_command", cmd_swaybg_command },
	{ "swaynag_command", cmd_swaynag_command },
	{ "workspace_layout", cmd_workspace_layout },
};

/* Runtime-only commands. Keep alphabetized */
static const struct cmd_handler command_handlers[] = {
	{ "border", cmd_border },
	{ "create_output", cmd_create_output },
	{ "exit", cmd_exit },
	{ "floating", cmd_floating },
	{ "fullscreen", cmd_fullscreen },
	{ "inhibit_idle", cmd_inhibit_idle },
	{ "kill", cmd_kill },
	{ "layout", cmd_layout },
	{ "mark", cmd_mark },
	{ "max_render_time", cmd_max_render_time },
	{ "move", cmd_move },
	{ "nop", cmd_nop },
	{ "opacity", cmd_opacity },
	{ "reload", cmd_reload },
	{ "rename", cmd_rename },
	{ "resize", cmd_resize },
	{ "scratchpad", cmd_scratchpad },
	{ "shortcuts_inhibitor", cmd_shortcuts_inhibitor },
	{ "split", cmd_split },
	{ "splith", cmd_splith },
	{ "splitt", cmd_splitt },
	{ "splitv", cmd_splitv },
	{ "sticky", cmd_sticky },
	{ "swap", cmd_swap },
	{ "title_format", cmd_title_format },
	{ "unmark", cmd_unmark },
	{ "urgent", cmd_urgent },
};

static int handler_compare(const void *_a, const void *_b) {
	const struct cmd_handler *a = _a;
	const struct cmd_handler *b = _b;
	return strcasecmp(a->command, b->command);
}

const struct cmd_handler *find_handler(char *line,
		const struct cmd_handler *handlers, size_t handlers_size) {
	if (!handlers || !handlers_size) {
		return NULL;
	}
	const struct cmd_handler query = { .command = line };
	return bsearch(&query, handlers,
			handlers_size / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
}

static const struct cmd_handler *find_handler_ex(char *line,
		const struct cmd_handler *config_handlers, size_t config_handlers_size,
		const struct cmd_handler *command_handlers, size_t command_handlers_size,
		const struct cmd_handler *handlers, size_t handlers_size) {
	const struct cmd_handler *handler = NULL;
	if (config->reading) {
		handler = find_handler(line, config_handlers, config_handlers_size);
	} else if (config->active) {
		handler = find_handler(line, command_handlers, command_handlers_size);
	}
	return handler ? handler : find_handler(line, handlers, handlers_size);
}

static const struct cmd_handler *find_core_handler(char *line) {
	return find_handler_ex(line, config_handlers, sizeof(config_handlers),
			command_handlers, sizeof(command_handlers),
			handlers, sizeof(handlers));
}

static void set_config_node(struct sway_node *node, bool node_overridden) {
	config->handler_context.node = node;
	config->handler_context.container = NULL;
	config->handler_context.workspace = NULL;
	config->handler_context.node_overridden = node_overridden;

	if (node == NULL) {
		return;
	}

	switch (node->type) {
	case N_CONTAINER:
		config->handler_context.container = node->sway_container;
		config->handler_context.workspace = node->sway_container->pending.workspace;
		break;
	case N_WORKSPACE:
		config->handler_context.workspace = node->sway_workspace;
		break;
	case N_ROOT:
	case N_OUTPUT:
		break;
	}
}

list_t *execute_command(char *_exec, struct sway_seat *seat,
		struct sway_container *con) {
	char *cmd;
	char matched_delim = ';';
	list_t *containers = NULL;
	bool using_criteria = false;

	if (seat == NULL) {
		// passing a NULL seat means we just pick the default seat
		seat = input_manager_get_default_seat();
		if (!sway_assert(seat, "could not find a seat to run the command on")) {
			return NULL;
		}
	}

	char *exec = strdup(_exec);
	char *head = exec;
	list_t *res_list = create_list();

	if (!res_list || !exec) {
		return NULL;
	}

	config->handler_context.seat = seat;

	do {
		for (; isspace(*head); ++head) {}
		// Extract criteria (valid for this command list only).
		if (matched_delim == ';') {
			using_criteria = false;
			if (*head == '[') {
				char *error = NULL;
				struct criteria *criteria = criteria_parse(head, &error);
				if (!criteria) {
					list_add(res_list,
							cmd_results_new(CMD_INVALID, "%s", error));
					free(error);
					goto cleanup;
				}
				list_free(containers);
				containers = criteria_get_containers(criteria);
				head += strlen(criteria->raw);
				criteria_destroy(criteria);
				using_criteria = true;
				// Skip leading whitespace
				for (; isspace(*head); ++head) {}
			}
		}
		// Split command list
		cmd = argsep(&head, ";,", &matched_delim);
		for (; isspace(*cmd); ++cmd) {}

		if (strcmp(cmd, "") == 0) {
			sway_log(SWAY_INFO, "Ignoring empty command.");
			continue;
		}
		sway_log(SWAY_INFO, "Handling command '%s'", cmd);
		//TODO better handling of argv
		int argc;
		char **argv = split_args(cmd, &argc);
		if (strcmp(argv[0], "exec") != 0 &&
				strcmp(argv[0], "exec_always") != 0 &&
				strcmp(argv[0], "mode") != 0) {
			for (int i = 1; i < argc; ++i) {
				if (*argv[i] == '\"' || *argv[i] == '\'') {
					strip_quotes(argv[i]);
				}
			}
		}
		const struct cmd_handler *handler = find_core_handler(argv[0]);
		if (!handler) {
			list_add(res_list, cmd_results_new(CMD_INVALID,
					"Unknown/invalid command '%s'", argv[0]));
			free_argv(argc, argv);
			goto cleanup;
		}

		// Var replacement, for all but first argument of set
		for (int i = handler->handle == cmd_set ? 2 : 1; i < argc; ++i) {
			argv[i] = do_var_replacement(argv[i]);
		}


		if (!using_criteria) {
			if (con) {
				set_config_node(&con->node, true);
			} else {
				set_config_node(seat_get_focus_inactive(seat, &root->node),
						false);
			}
			struct cmd_results *res = handler->handle(argc-1, argv+1);
			list_add(res_list, res);
			if (res->status == CMD_INVALID) {
				free_argv(argc, argv);
				goto cleanup;
			}
		} else if (containers->length == 0) {
			list_add(res_list,
					cmd_results_new(CMD_FAILURE, "No matching node."));
		} else {
			struct cmd_results *fail_res = NULL;
			for (int i = 0; i < containers->length; ++i) {
				struct sway_container *container = containers->items[i];
				set_config_node(&container->node, true);
				struct cmd_results *res = handler->handle(argc-1, argv+1);
				if (res->status == CMD_SUCCESS) {
					free_cmd_results(res);
				} else {
					// last failure will take precedence
					if (fail_res) {
						free_cmd_results(fail_res);
					}
					fail_res = res;
					if (res->status == CMD_INVALID) {
						list_add(res_list, fail_res);
						free_argv(argc, argv);
						goto cleanup;
					}
				}
			}
			list_add(res_list,
					fail_res ? fail_res : cmd_results_new(CMD_SUCCESS, NULL));
		}
		free_argv(argc, argv);
	} while(head);
cleanup:
	free(exec);
	list_free(containers);
	return res_list;
}

// this is like execute_command above, except:
// 1) it ignores empty commands (empty lines)
// 2) it does variable substitution
// 3) it doesn't split commands (because the multiple commands are supposed to
//	  be chained together)
// 4) execute_command handles all state internally while config_command has
// some state handled outside (notably the block mode, in read_config)
struct cmd_results *config_command(char *exec, char **new_block) {
	struct cmd_results *results = NULL;
	int argc;
	char **argv = split_args(exec, &argc);

	// Check for empty lines
	if (!argc) {
		results = cmd_results_new(CMD_SUCCESS, NULL);
		goto cleanup;
	}

	// Check for the start of a block
	if (argc > 1 && strcmp(argv[argc - 1], "{") == 0) {
		*new_block = join_args(argv, argc - 1);
		results = cmd_results_new(CMD_BLOCK, NULL);
		goto cleanup;
	}

	// Check for the end of a block
	if (strcmp(argv[argc - 1], "}") == 0) {
		results = cmd_results_new(CMD_BLOCK_END, NULL);
		goto cleanup;
	}

	// Make sure the command is not stored in a variable
	if (*argv[0] == '$') {
		argv[0] = do_var_replacement(argv[0]);
		char *temp = join_args(argv, argc);
		free_argv(argc, argv);
		argv = split_args(temp, &argc);
		free(temp);
		if (!argc) {
			results = cmd_results_new(CMD_SUCCESS, NULL);
			goto cleanup;
		}
	}

	// Determine the command handler
	sway_log(SWAY_INFO, "Config command: %s", exec);
	const struct cmd_handler *handler = find_core_handler(argv[0]);
	if (!handler || !handler->handle) {
		const char *error = handler
			? "Command '%s' is shimmed, but unimplemented"
			: "Unknown/invalid command '%s'";
		results = cmd_results_new(CMD_INVALID, error, argv[0]);
		goto cleanup;
	}

	// Do variable replacement
	if (handler->handle == cmd_set && argc > 1 && *argv[1] == '$') {
		// Escape the variable name so it does not get replaced by one shorter
		char *temp = calloc(1, strlen(argv[1]) + 2);
		temp[0] = '$';
		strcpy(&temp[1], argv[1]);
		free(argv[1]);
		argv[1] = temp;
	}
	char *command = do_var_replacement(join_args(argv, argc));
	sway_log(SWAY_INFO, "After replacement: %s", command);
	free_argv(argc, argv);
	argv = split_args(command, &argc);
	free(command);

	// Strip quotes and unescape the string
	for (int i = handler->handle == cmd_set ? 2 : 1; i < argc; ++i) {
		if (handler->handle != cmd_exec && handler->handle != cmd_exec_always
				&& handler->handle != cmd_mode
				&& handler->handle != cmd_bindsym
				&& handler->handle != cmd_bindcode
				&& handler->handle != cmd_bindswitch
				&& handler->handle != cmd_set
				&& handler->handle != cmd_for_window
				&& (*argv[i] == '\"' || *argv[i] == '\'')) {
			strip_quotes(argv[i]);
		}
		unescape_string(argv[i]);
	}

	// Run command
	results = handler->handle(argc - 1, argv + 1);

cleanup:
	free_argv(argc, argv);
	return results;
}

struct cmd_results *config_subcommand(char **argv, int argc,
		const struct cmd_handler *handlers, size_t handlers_size) {
	char *command = join_args(argv, argc);
	sway_log(SWAY_DEBUG, "Subcommand: %s", command);
	free(command);

	const struct cmd_handler *handler = find_handler(argv[0], handlers,
			handlers_size);
	if (!handler) {
		return cmd_results_new(CMD_INVALID,
				"Unknown/invalid command '%s'", argv[0]);
	}
	if (handler->handle) {
		return handler->handle(argc - 1, argv + 1);
	}
	return cmd_results_new(CMD_INVALID,
			"The command '%s' is shimmed, but unimplemented", argv[0]);
}

struct cmd_results *config_commands_command(char *exec) {
	struct cmd_results *results = NULL;
	int argc;
	char **argv = split_args(exec, &argc);
	if (!argc) {
		results = cmd_results_new(CMD_SUCCESS, NULL);
		goto cleanup;
	}

	// Find handler for the command this is setting a policy for
	char *cmd = argv[0];

	if (strcmp(cmd, "}") == 0) {
		results = cmd_results_new(CMD_BLOCK_END, NULL);
		goto cleanup;
	}

	const struct cmd_handler *handler = find_handler(cmd, NULL, 0);
	if (!handler && strcmp(cmd, "*") != 0) {
		results = cmd_results_new(CMD_INVALID,
			"Unknown/invalid command '%s'", cmd);
		goto cleanup;
	}

	enum command_context context = 0;

	struct {
		char *name;
		enum command_context context;
	} context_names[] = {
		{ "config", CONTEXT_CONFIG },
		{ "binding", CONTEXT_BINDING },
		{ "ipc", CONTEXT_IPC },
		{ "criteria", CONTEXT_CRITERIA },
		{ "all", CONTEXT_ALL },
	};

	for (int i = 1; i < argc; ++i) {
		size_t j;
		for (j = 0; j < sizeof(context_names) / sizeof(context_names[0]); ++j) {
			if (strcmp(context_names[j].name, argv[i]) == 0) {
				break;
			}
		}
		if (j == sizeof(context_names) / sizeof(context_names[0])) {
			results = cmd_results_new(CMD_INVALID,
					"Invalid command context %s", argv[i]);
			goto cleanup;
		}
		context |= context_names[j].context;
	}

	results = cmd_results_new(CMD_SUCCESS, NULL);

cleanup:
	free_argv(argc, argv);
	return results;
}

struct cmd_results *cmd_results_new(enum cmd_status status,
		const char *format, ...) {
	struct cmd_results *results = malloc(sizeof(struct cmd_results));
	if (!results) {
		sway_log(SWAY_ERROR, "Unable to allocate command results");
		return NULL;
	}
	results->status = status;
	if (format) {
		char *error = malloc(256);
		va_list args;
		va_start(args, format);
		if (error) {
			vsnprintf(error, 256, format, args);
		}
		va_end(args);
		results->error = error;
	} else {
		results->error = NULL;
	}
	return results;
}

void free_cmd_results(struct cmd_results *results) {
	if (results->error) {
		free(results->error);
	}
	free(results);
}

char *cmd_results_to_json(list_t *res_list) {
	json_object *result_array = json_object_new_array();
	for (int i = 0; i < res_list->length; ++i) {
		struct cmd_results *results = res_list->items[i];
		json_object *root = json_object_new_object();
		json_object_object_add(root, "success",
				json_object_new_boolean(results->status == CMD_SUCCESS));
		if (results->error) {
			json_object_object_add(root, "parse_error",
					json_object_new_boolean(results->status == CMD_INVALID));
			json_object_object_add(
					root, "error", json_object_new_string(results->error));
		}
		json_object_array_add(result_array, root);
	}
	const char *json = json_object_to_json_string(result_array);
	char *res = strdup(json);
	json_object_put(result_array);
	return res;
}
