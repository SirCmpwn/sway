
#include <string.h>
#include <strings.h>
#include "log.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"

struct cmd_results *cmd_color(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "color", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	struct sway_container *container = config->handler_context.container;
	if (!container || !container->view) {
		return cmd_results_new(CMD_INVALID, "Only views can have color profiles");
	}
	struct sway_view *view = container->view;

	wlr_color_config_free(view->color);
	view->color = NULL;

	if (strcasecmp(argv[0], "profile") == 0) {
		if(strlen(argv[1]) != 0) {
			view->color = wlr_color_config_load(argv[1]);
			if(! view->color) {
				config_add_swaynag_warning("error loading color profile '%s'", argv[1]);
			}
		}
		argc -= 2; argv += 2;
	} else {
		return cmd_results_new(CMD_INVALID, "Invalid view color config.");
	}

	config->handler_context.leftovers.argc = argc;
	config->handler_context.leftovers.argv = argv;
	return cmd_results_new(CMD_SUCCESS, NULL);
}

