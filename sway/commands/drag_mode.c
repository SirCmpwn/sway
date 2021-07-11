#include "sway/commands.h"
#include "util.h"

struct cmd_results *cmd_drag_mode(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "drag_mode", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	config->drag_mode = parse_boolean(argv[0], config->drag_mode);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
