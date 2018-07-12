#include <stdlib.h>
#include <string.h>
#include "sway/config.h"
#include "sway/commands.h"
#include "sway/input/input-manager.h"

struct cmd_results *input_cmd_repeat_rate(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "repeat_rate", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	struct input_config *current_input_config =
		config->handler_context.input_config;
	if (!current_input_config) {
		return cmd_results_new(CMD_FAILURE,
			"repeat_rate", "No input device defined.");
	}
	struct input_config *new_config =
		new_input_config(current_input_config->identifier);

	int repeat_rate = atoi(argv[0]);
	if (repeat_rate < 0) {
		return cmd_results_new(CMD_INVALID, "repeat_rate",
			"Repeat rate cannot be negative");
	}
	new_config->repeat_rate = repeat_rate;

	apply_input_config(new_config);
	return cmd_results_new(CMD_SUCCESS, NULL, NULL);
}
