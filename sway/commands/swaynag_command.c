#define _XOPEN_SOURCE 700 // for strdup
#include <string.h>
#include "sway/commands.h"
#include "log.h"
#include "stringop.h"

struct cmd_results *cmd_swaynag_command(int argc, char **argv) {
	struct cmd_results *error = NULL;
	char *new_label = NULL;
	if ((error = checkarg(argc, "swaynag_command", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (strcmp(argv[0], "--label") == 0) {
		if ((error = checkarg(argc, "swaynag_command", EXPECTED_AT_LEAST, 3))) {
			return error;
		}
		new_label = strdup(argv[1]);
		argv += 2;
		argc -= 2;
	}

	free(config->swaynag_command);
	config->swaynag_command = NULL;
	free(config->swaynag_label);
	config->swaynag_label = new_label;

	char *new_command = join_args(argv, argc);
	if (strcmp(new_command, "-") != 0) {
		config->swaynag_command = new_command;
		sway_log(SWAY_DEBUG, "Using custom swaynag command: %s",
				config->swaynag_command);
	} else {
		free(new_command);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
