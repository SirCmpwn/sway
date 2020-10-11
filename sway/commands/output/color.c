
#include <string.h>
#include <strings.h>
#include "log.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/output.h"

struct cmd_results *output_cmd_color(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}

	if (argc < 2) {
		return cmd_results_new(CMD_INVALID, "Missing color profile argument.");
	}

	struct output_config *oc = config->handler_context.output_config;
	wlr_color_config_free(oc->color);
	oc->color = NULL;

	if (strcasecmp(argv[0], "profile") == 0) {
		if(strlen(argv[1]) != 0) {
			oc->color = wlr_color_config_load(argv[1]);
			if(! oc->color) {
				config_add_swaynag_warning("error loading color profile '%s'", argv[1]);
			}
		}
		argc -= 2; argv += 2;
	} else {
		return cmd_results_new(CMD_INVALID, "Invalid output color config.");
	}

	config->handler_context.leftovers.argc = argc;
	config->handler_context.leftovers.argv = argv;
	return NULL;
}
