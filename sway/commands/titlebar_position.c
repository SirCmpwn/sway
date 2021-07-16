#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"

struct cmd_results *cmd_titlebar_position(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "titlebar_position", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcmp(argv[0], "top") == 0) {
		config->titlebar_position = TITLEBAR_TOP;
	} else if (strcmp(argv[0], "bottom") == 0) {
		config->titlebar_position = TITLEBAR_BOTTOM;
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected 'titlebar_position top|bottom'");
	}

	arrange_root();
	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		output_damage_whole(output);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

