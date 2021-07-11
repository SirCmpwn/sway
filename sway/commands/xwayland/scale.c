#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/server.h"
#include "log.h"

struct cmd_results *xwayland_cmd_scale(int argc, char **argv) {
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing scale argument.");
	}

	char *end;
	int32_t scale = strtol(*argv, &end, 10);
	if (*end) {
		return cmd_results_new(CMD_INVALID, "Invalid scale.");
	}
	if(scale < 1) {
		return cmd_results_new(CMD_INVALID, "Invalid scale: must be 1 or higher");
	}

	config->xwayland_scale = scale;

	if(server.xwayland.wlr_xwayland != NULL) {
		wlr_xwayland_set_scale(server.xwayland.wlr_xwayland, scale);
	}

	config->handler_context.leftovers.argc = argc - 1;
	config->handler_context.leftovers.argv = argv + 1;

	return NULL;
}
