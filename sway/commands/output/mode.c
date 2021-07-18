#include <strings.h>
#include "sway/commands.h"
#include "sway/config.h"

static bool parse_modeline(char **argv, drmModeModeInfo *mode) {
	mode->type = DRM_MODE_TYPE_USERDEF;
	mode->clock = strtof(argv[0], NULL) * 1000;
	mode->hdisplay = strtol(argv[1], NULL, 10);
	mode->hsync_start = strtol(argv[2], NULL, 10);
	mode->hsync_end = strtol(argv[3], NULL, 10);
	mode->htotal = strtol(argv[4], NULL, 10);
	mode->vdisplay = strtol(argv[5], NULL, 10);
	mode->vsync_start = strtol(argv[6], NULL, 10);
	mode->vsync_end = strtol(argv[7], NULL, 10);
	mode->vtotal = strtol(argv[8], NULL, 10);

	mode->vrefresh = mode->clock * 1000.0 * 1000.0
		/ mode->htotal / mode->vtotal;
	if (strcasecmp(argv[9], "+hsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_PHSYNC;
	} else if (strcasecmp(argv[9], "-hsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_NHSYNC;
	} else {
		return false;
	}

	if (strcasecmp(argv[10], "+vsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_PVSYNC;
	} else if (strcasecmp(argv[10], "-vsync") == 0) {
		mode->flags |= DRM_MODE_FLAG_NVSYNC;
	} else {
		return false;
	}

	snprintf(mode->name, sizeof(mode->name), "%dx%d@%d",
		 mode->hdisplay, mode->vdisplay, mode->vrefresh / 1000);

	return true;
}

struct cmd_results *output_cmd_mode(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing mode argument.");
	}

	struct output_config *output = config->handler_context.output_config;

	if (strcmp(argv[0], "--custom") == 0) {
		argv++;
		argc--;
		output->custom_mode = 1;
	} else {
		output->custom_mode = 0;
	}

	if (argc == 11) {
		// Format is modeline
		if (!parse_modeline(argv, &output->drm_mode)) {
			return cmd_results_new(CMD_INVALID, "Invalid modeline");
		}
		output->width = output->drm_mode.hdisplay;
		output->height = output->drm_mode.vdisplay;
		argv += 11;
		argc -= 11;
	} else {
		char *end;
		output->width = strtol(*argv, &end, 10);
		if (*end) {
			// Format is 1234x4321
			if (*end != 'x') {
				return cmd_results_new(CMD_INVALID, "Invalid mode width.");
			}
			++end;
			output->height = strtol(end, &end, 10);
			if (*end) {
				if (*end != '@') {
					return cmd_results_new(CMD_INVALID, "Invalid mode height.");
				}
				++end;
				output->refresh_rate = strtof(end, &end);
				if (strcasecmp("Hz", end) != 0) {
					return cmd_results_new(CMD_INVALID,
						"Invalid mode refresh rate.");
				}
			}
		} else {
			// Format is 1234 4321
			argc--; argv++;
			if (!argc) {
				return cmd_results_new(CMD_INVALID,
					"Missing mode argument (height).");
			}
			output->height = strtol(*argv, &end, 10);
			if (*end) {
				return cmd_results_new(CMD_INVALID, "Invalid mode height.");
			}
		}
	}

	config->handler_context.leftovers.argc = argc - 1;
	config->handler_context.leftovers.argv = argv + 1;
	return NULL;
}

