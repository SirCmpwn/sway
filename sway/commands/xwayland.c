#include "sway/config.h"
#include "log.h"
#include "sway/commands.h"
#include "sway/server.h"
#include "util.h"

// must be in order for the bsearch
static struct cmd_handler xwayland_handlers[] = {
	{ "enable", xwayland_cmd_enable },
	{ "disable", xwayland_cmd_disable },
	{ "force", xwayland_cmd_force },
	{ "scale", xwayland_cmd_scale },
};

struct cmd_results *cmd_xwayland(int argc, char **argv) {
#ifdef HAVE_XWAYLAND
	struct cmd_results *error;
	while (argc > 0) {
		config->handler_context.leftovers.argc = 0;
		config->handler_context.leftovers.argv = NULL;

		if (find_handler(*argv, xwayland_handlers, sizeof(xwayland_handlers))) {
			error = config_subcommand(argv, argc, xwayland_handlers,
					sizeof(xwayland_handlers));
		} else {
			error = cmd_results_new(CMD_INVALID,
				"Invalid output subcommand: %s.", *argv);
		}

		if (error != NULL) {
			return error;
		}

		argc = config->handler_context.leftovers.argc;
		argv = config->handler_context.leftovers.argv;
	}

	config->handler_context.leftovers.argc = 0;
	config->handler_context.leftovers.argv = NULL;
#else
	sway_log(SWAY_INFO, "Ignoring `xwayland` command, "
		"sway hasn't been built with Xwayland support");
#endif

	return cmd_results_new(CMD_SUCCESS, NULL);
}
