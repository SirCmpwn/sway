#include <string.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "log.h"

struct cmd_results *cmd_titlebar_v_height(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "titlebar_v_height", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	char *inv;
	int value = strtol(argv[0], &inv, 10);
	if (*inv != '\0' || value < -1 ) {
		return cmd_results_new(CMD_FAILURE, "Invalid size specified");
	}

	config->titlebar_v_height = value;

	for (int i = 0; i < root->outputs->length; ++i) {
		struct sway_output *output = root->outputs->items[i];
		arrange_workspace(output_get_active_workspace(output));
		output_damage_whole(output);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
