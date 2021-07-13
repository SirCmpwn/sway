#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sway/config.h"
#include "sway/commands.h"
#include "sway/input/input-manager.h"
#include "util.h"

struct cmd_results *input_cmd_sensitivity(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "sensitivity", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	struct input_config *ic = config->handler_context.input_config;
	if (!ic) {
		return cmd_results_new(CMD_FAILURE, "No input device defined.");
	}

	float sensitivity = parse_float(argv[0]);
	if (isnan(sensitivity)) {
		return cmd_results_new(CMD_INVALID,
			"Invalid sensitivity; expected float.");
	} else if (sensitivity < 0) {
		return cmd_results_new(CMD_INVALID,
			"Sensitivity cannot be negative.");
	}
	ic->sensitivity = sensitivity;

	return cmd_results_new(CMD_SUCCESS, NULL);
}
