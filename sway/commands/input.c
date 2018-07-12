#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/input/input-manager.h"
#include "log.h"
#include "stringop.h"

// must be in order for the bsearch
static struct cmd_handler input_handlers[] = {
	{ "accel_profile", input_cmd_accel_profile },
	{ "click_method", input_cmd_click_method },
	{ "drag_lock", input_cmd_drag_lock },
	{ "dwt", input_cmd_dwt },
	{ "events", input_cmd_events },
	{ "left_handed", input_cmd_left_handed },
	{ "map_from_region", input_cmd_map_from_region },
	{ "map_to_output", input_cmd_map_to_output },
	{ "middle_emulation", input_cmd_middle_emulation },
	{ "natural_scroll", input_cmd_natural_scroll },
	{ "pointer_accel", input_cmd_pointer_accel },
	{ "repeat_delay", input_cmd_repeat_delay },
	{ "repeat_rate", input_cmd_repeat_rate },
	{ "scroll_method", input_cmd_scroll_method },
	{ "tap", input_cmd_tap },
	{ "xkb_layout", input_cmd_xkb_layout },
	{ "xkb_model", input_cmd_xkb_model },
	{ "xkb_options", input_cmd_xkb_options },
	{ "xkb_rules", input_cmd_xkb_rules },
	{ "xkb_variant", input_cmd_xkb_variant },
};

struct cmd_results *cmd_input(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "input", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	wlr_log(L_DEBUG, "entering input block: %s", argv[0]);

	config->handler_context.input_config = new_input_config(argv[0]);
	if (!config->handler_context.input_config) {
		return cmd_results_new(CMD_FAILURE, NULL, "Couldn't allocate config");
	}

	struct cmd_results *res = config_subcommand(argv + 1, argc - 1,
			input_handlers, sizeof(input_handlers));

	free_input_config(config->handler_context.input_config);
	config->handler_context.input_config = NULL;

	return res;
}
