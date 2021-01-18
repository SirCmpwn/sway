#define _XOPEN_SOURCE 700 // for strdup
#include <string.h>
#include "sway/commands.h"
#include "log.h"
#include "stringop.h"

struct cmd_results *bar_cmd_swaybar_command(int argc, char **argv) {
	struct cmd_results *error = NULL;
	char *new_label = NULL;
	if ((error = checkarg(argc, "swaybar_command", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (strcmp(argv[0], "--label") == 0) {
		if ((error = checkarg(argc, "swaybar_command", EXPECTED_AT_LEAST, 3))) {
			return error;
		}
		new_label = strdup(argv[1]);
		argv += 2;
		argc -= 2;
	}
	free(config->current_bar->swaybar_command);
	config->current_bar->swaybar_command = join_args(argv, argc);
	free(config->current_bar->swaybar_label);
	config->current_bar->swaybar_label = new_label;
	sway_log(SWAY_DEBUG, "Using custom swaybar command: %s",
			config->current_bar->swaybar_command);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
