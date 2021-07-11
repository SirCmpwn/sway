#ifndef _SWAY_COMMANDS_H
#define _SWAY_COMMANDS_H

#include <wlr/util/edges.h>
#include "config.h"

struct sway_container;

typedef struct cmd_results *sway_cmd(int argc, char **argv);

struct cmd_handler {
	char *command;
	sway_cmd *handle;
};

/**
 * Indicates the result of a command's execution.
 */
enum cmd_status {
	CMD_SUCCESS, 		/**< The command was successful */
	CMD_FAILURE,		/**< The command resulted in an error */
	CMD_INVALID, 		/**< Unknown command or parser error */
	CMD_DEFER,		/**< Command execution deferred */
	CMD_BLOCK,
	CMD_BLOCK_COMMANDS,
	CMD_BLOCK_END
};

/**
 * Stores the result of executing a command.
 */
struct cmd_results {
	enum cmd_status status;
	/**
	 * Human friendly error message, or NULL on success
	 */
	char *error;
};

enum expected_args {
	EXPECTED_AT_LEAST,
	EXPECTED_AT_MOST,
	EXPECTED_EQUAL_TO
};

struct cmd_results *checkarg(int argc, const char *name,
		enum expected_args type, int val);

const struct cmd_handler *find_handler(char *line,
		const struct cmd_handler *cmd_handlers, size_t handlers_size);

/**
 * Parse and executes a command.
 *
 * If the command string contains criteria then the command will be executed on
 * all matching containers. Otherwise, it'll run on the `con` container. If
 * `con` is NULL then it'll run on the currently focused container.
 */
list_t *execute_command(char *command,  struct sway_seat *seat,
		struct sway_container *con);
/**
 * Parse and handles a command during config file loading.
 *
 * Do not use this under normal conditions.
 */
struct cmd_results *config_command(char *command, char **new_block);
/**
 * Parse and handle a sub command
 */
struct cmd_results *config_subcommand(char **argv, int argc,
		const struct cmd_handler *handlers, size_t handlers_size);
/*
 * Parses a command policy rule.
 */
struct cmd_results *config_commands_command(char *exec);
/**
 * Allocates a cmd_results object.
 */
struct cmd_results *cmd_results_new(enum cmd_status status, const char *error, ...);
/**
 * Frees a cmd_results object.
 */
void free_cmd_results(struct cmd_results *results);
/**
 * Serializes a list of cmd_results to a JSON string.
 *
 * Free the JSON string later on.
 */
char *cmd_results_to_json(list_t *res_list);

/**
 * TODO: Move this function and its dependent functions to container.c.
 */
void container_resize_tiled(struct sway_container *parent, uint32_t axis,
		int amount);

struct sway_container *container_find_resize_parent(struct sway_container *con,
		uint32_t edge);

/**
 * Handlers shared by exec and exec_always.
 */
sway_cmd cmd_exec_validate;
sway_cmd cmd_exec_process;

sway_cmd cmd_assign;
sway_cmd cmd_bar;
sway_cmd cmd_bindcode;
sway_cmd cmd_bindswitch;
sway_cmd cmd_bindsym;
sway_cmd cmd_border;
sway_cmd cmd_client_noop;
sway_cmd cmd_client_focused;
sway_cmd cmd_client_focused_inactive;
sway_cmd cmd_client_unfocused;
sway_cmd cmd_client_urgent;
sway_cmd cmd_client_placeholder;
sway_cmd cmd_client_background;
sway_cmd cmd_commands;
sway_cmd cmd_create_output;
sway_cmd cmd_default_border;
sway_cmd cmd_default_floating_border;
sway_cmd cmd_default_orientation;
sway_cmd cmd_exec;
sway_cmd cmd_exec_always;
sway_cmd cmd_exit;
sway_cmd cmd_floating;
sway_cmd cmd_floating_maximum_size;
sway_cmd cmd_floating_minimum_size;
sway_cmd cmd_floating_modifier;
sway_cmd cmd_floating_scroll;
sway_cmd cmd_focus;
sway_cmd cmd_focus_follows_mouse;
sway_cmd cmd_focus_on_window_activation;
sway_cmd cmd_focus_wrapping;
sway_cmd cmd_font;
sway_cmd cmd_for_window;
sway_cmd cmd_force_display_urgency_hint;
sway_cmd cmd_force_focus_wrapping;
sway_cmd cmd_fullscreen;
sway_cmd cmd_gaps;
sway_cmd cmd_hide_edge_borders;
sway_cmd cmd_include;
sway_cmd cmd_inhibit_idle;
sway_cmd cmd_input;
sway_cmd cmd_seat;
sway_cmd cmd_ipc;
sway_cmd cmd_kill;
sway_cmd cmd_layout;
sway_cmd cmd_log_colors;
sway_cmd cmd_mark;
sway_cmd cmd_max_render_time;
sway_cmd cmd_mode;
sway_cmd cmd_mouse_warping;
sway_cmd cmd_move;
sway_cmd cmd_new_float;
sway_cmd cmd_new_window;
sway_cmd cmd_nop;
sway_cmd cmd_opacity;
sway_cmd cmd_new_float;
sway_cmd cmd_new_window;
sway_cmd cmd_no_focus;
sway_cmd cmd_output;
sway_cmd cmd_permit;
sway_cmd cmd_popup_during_fullscreen;
sway_cmd cmd_reject;
sway_cmd cmd_reload;
sway_cmd cmd_rename;
sway_cmd cmd_resize;
sway_cmd cmd_scratchpad;
sway_cmd cmd_seamless_mouse;
sway_cmd cmd_set;
sway_cmd cmd_shortcuts_inhibitor;
sway_cmd cmd_show_marks;
sway_cmd cmd_smart_borders;
sway_cmd cmd_smart_gaps;
sway_cmd cmd_split;
sway_cmd cmd_splith;
sway_cmd cmd_splitt;
sway_cmd cmd_splitv;
sway_cmd cmd_sticky;
sway_cmd cmd_swaybg_command;
sway_cmd cmd_swaynag_command;
sway_cmd cmd_swap;
sway_cmd cmd_tiling_drag;
sway_cmd cmd_tiling_drag_threshold;
sway_cmd cmd_title_align;
sway_cmd cmd_title_format;
sway_cmd cmd_titlebar_border_thickness;
sway_cmd cmd_titlebar_padding;
sway_cmd cmd_unbindcode;
sway_cmd cmd_unbindswitch;
sway_cmd cmd_unbindsym;
sway_cmd cmd_unmark;
sway_cmd cmd_urgent;
sway_cmd cmd_workspace;
sway_cmd cmd_workspace_layout;
sway_cmd cmd_ws_auto_back_and_forth;
sway_cmd cmd_xwayland;

sway_cmd bar_cmd_bindcode;
sway_cmd bar_cmd_binding_mode_indicator;
sway_cmd bar_cmd_bindsym;
sway_cmd bar_cmd_colors;
sway_cmd bar_cmd_font;
sway_cmd bar_cmd_gaps;
sway_cmd bar_cmd_mode;
sway_cmd bar_cmd_modifier;
sway_cmd bar_cmd_output;
sway_cmd bar_cmd_height;
sway_cmd bar_cmd_hidden_state;
sway_cmd bar_cmd_icon_theme;
sway_cmd bar_cmd_id;
sway_cmd bar_cmd_position;
sway_cmd bar_cmd_separator_symbol;
sway_cmd bar_cmd_status_command;
sway_cmd bar_cmd_status_edge_padding;
sway_cmd bar_cmd_status_padding;
sway_cmd bar_cmd_pango_markup;
sway_cmd bar_cmd_strip_workspace_numbers;
sway_cmd bar_cmd_strip_workspace_name;
sway_cmd bar_cmd_swaybar_command;
sway_cmd bar_cmd_tray_bindcode;
sway_cmd bar_cmd_tray_bindsym;
sway_cmd bar_cmd_tray_output;
sway_cmd bar_cmd_tray_padding;
sway_cmd bar_cmd_unbindcode;
sway_cmd bar_cmd_unbindsym;
sway_cmd bar_cmd_wrap_scroll;
sway_cmd bar_cmd_workspace_buttons;
sway_cmd bar_cmd_workspace_min_width;

sway_cmd bar_colors_cmd_active_workspace;
sway_cmd bar_colors_cmd_background;
sway_cmd bar_colors_cmd_focused_background;
sway_cmd bar_colors_cmd_binding_mode;
sway_cmd bar_colors_cmd_focused_workspace;
sway_cmd bar_colors_cmd_inactive_workspace;
sway_cmd bar_colors_cmd_separator;
sway_cmd bar_colors_cmd_focused_separator;
sway_cmd bar_colors_cmd_statusline;
sway_cmd bar_colors_cmd_focused_statusline;
sway_cmd bar_colors_cmd_urgent_workspace;

sway_cmd input_cmd_seat;
sway_cmd input_cmd_accel_profile;
sway_cmd input_cmd_calibration_matrix;
sway_cmd input_cmd_click_method;
sway_cmd input_cmd_drag;
sway_cmd input_cmd_drag_lock;
sway_cmd input_cmd_dwt;
sway_cmd input_cmd_events;
sway_cmd input_cmd_left_handed;
sway_cmd input_cmd_map_from_region;
sway_cmd input_cmd_map_to_output;
sway_cmd input_cmd_map_to_region;
sway_cmd input_cmd_middle_emulation;
sway_cmd input_cmd_natural_scroll;
sway_cmd input_cmd_pointer_accel;
sway_cmd input_cmd_scroll_factor;
sway_cmd input_cmd_repeat_delay;
sway_cmd input_cmd_repeat_rate;
sway_cmd input_cmd_scroll_button;
sway_cmd input_cmd_scroll_method;
sway_cmd input_cmd_tap;
sway_cmd input_cmd_tap_button_map;
sway_cmd input_cmd_tool_mode;
sway_cmd input_cmd_xkb_capslock;
sway_cmd input_cmd_xkb_file;
sway_cmd input_cmd_xkb_layout;
sway_cmd input_cmd_xkb_model;
sway_cmd input_cmd_xkb_numlock;
sway_cmd input_cmd_xkb_options;
sway_cmd input_cmd_xkb_rules;
sway_cmd input_cmd_xkb_switch_layout;
sway_cmd input_cmd_xkb_variant;

sway_cmd output_cmd_adaptive_sync;
sway_cmd output_cmd_background;
sway_cmd output_cmd_disable;
sway_cmd output_cmd_dpms;
sway_cmd output_cmd_enable;
sway_cmd output_cmd_max_render_time;
sway_cmd output_cmd_mode;
sway_cmd output_cmd_position;
sway_cmd output_cmd_scale;
sway_cmd output_cmd_scale_filter;
sway_cmd output_cmd_subpixel;
sway_cmd output_cmd_toggle;
sway_cmd output_cmd_transform;

sway_cmd seat_cmd_attach;
sway_cmd seat_cmd_cursor;
sway_cmd seat_cmd_fallback;
sway_cmd seat_cmd_hide_cursor;
sway_cmd seat_cmd_idle_inhibit;
sway_cmd seat_cmd_idle_wake;
sway_cmd seat_cmd_keyboard_grouping;
sway_cmd seat_cmd_pointer_constraint;
sway_cmd seat_cmd_shortcuts_inhibitor;
sway_cmd seat_cmd_xcursor_theme;

sway_cmd xwayland_cmd_enable;
sway_cmd xwayland_cmd_disable;
sway_cmd xwayland_cmd_force;
sway_cmd xwayland_cmd_scale;

sway_cmd cmd_ipc_cmd;
sway_cmd cmd_ipc_events;
sway_cmd cmd_ipc_event_cmd;

#endif
