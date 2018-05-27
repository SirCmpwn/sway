#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <strings.h>
#include "sway/output.h"
#include "sway/tree/output.h"
#include "sway/tree/workspace.h"
#include "log.h"

struct sway_container *output_create(
		struct sway_output *sway_output) {
	struct wlr_box size;
	wlr_output_effective_resolution(sway_output->wlr_output, &size.width,
		&size.height);

	const char *name = sway_output->wlr_output->name;
	char identifier[128];
	output_get_identifier(identifier, sizeof(identifier), sway_output);

	struct output_config *oc = NULL, *all = NULL;
	for (int i = 0; i < config->output_configs->length; ++i) {
		struct output_config *cur = config->output_configs->items[i];

		if (strcasecmp(name, cur->name) == 0 ||
				strcasecmp(identifier, cur->name) == 0) {
			sway_log(L_DEBUG, "Matched output config for %s", name);
			oc = cur;
		}
		if (strcasecmp("*", cur->name) == 0) {
			sway_log(L_DEBUG, "Matched wildcard output config for %s", name);
			all = cur;
		}

		if (oc && all) {
			break;
		}
	}
	if (!oc) {
		oc = all;
	}

	if (oc && !oc->enabled) {
		return NULL;
	}

	struct sway_container *output = container_create(C_OUTPUT);
	output->sway_output = sway_output;
	output->name = strdup(name);
	if (output->name == NULL) {
		container_destroy(output);
		return NULL;
	}

	apply_output_config(oc, output);
	container_add_child(&root_container, output);
	load_swaybars();

	// Create workspace
	char *ws_name = workspace_next_name(output->name);
	sway_log(L_DEBUG, "Creating default workspace %s", ws_name);
	struct sway_container *ws = workspace_create(output, ws_name);
	// Set each seat's focus if not already set
	struct sway_seat *seat = NULL;
	wl_list_for_each(seat, &input_manager->seats, link) {
		if (!seat->has_focus) {
			seat_set_focus(seat, ws);
		}
	}

	free(ws_name);
	container_create_notify(output);
	return output;
}

