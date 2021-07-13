#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include "sway/config.h"
#include "sway/input/keyboard.h"
#include "log.h"

struct input_config *new_input_config(const char* identifier) {
	struct input_config *input = calloc(1, sizeof(struct input_config));
	if (!input) {
		sway_log(SWAY_DEBUG, "Unable to allocate input config");
		return NULL;
	}
	sway_log(SWAY_DEBUG, "new_input_config(%s)", identifier);
	if (!(input->identifier = strdup(identifier))) {
		free(input);
		sway_log(SWAY_DEBUG, "Unable to allocate input config");
		return NULL;
	}

	input->input_type = NULL;
	input->tap = INT_MIN;
	input->tap_button_map = INT_MIN;
	input->drag = INT_MIN;
	input->drag_lock = INT_MIN;
	input->dwt = INT_MIN;
	input->send_events = INT_MIN;
	input->click_method = INT_MIN;
	input->middle_emulation = INT_MIN;
	input->natural_scroll = INT_MIN;
	input->accel_profile = INT_MIN;
	input->pointer_accel = FLT_MIN;
	input->sensitivity = FLT_MIN;
	input->scroll_factor = FLT_MIN;
	input->scroll_button = INT_MIN;
	input->scroll_method = INT_MIN;
	input->left_handed = INT_MIN;
	input->repeat_delay = INT_MIN;
	input->repeat_rate = INT_MIN;
	input->xkb_numlock = INT_MIN;
	input->xkb_capslock = INT_MIN;
	input->xkb_file_is_set = false;
	input->tools = create_list();

	return input;
}

void merge_input_config(struct input_config *dst, struct input_config *src) {
	if (src->accel_profile != INT_MIN) {
		dst->accel_profile = src->accel_profile;
	}
	if (src->click_method != INT_MIN) {
		dst->click_method = src->click_method;
	}
	if (src->drag != INT_MIN) {
		dst->drag = src->drag;
	}
	if (src->drag_lock != INT_MIN) {
		dst->drag_lock = src->drag_lock;
	}
	if (src->dwt != INT_MIN) {
		dst->dwt = src->dwt;
	}
	if (src->left_handed != INT_MIN) {
		dst->left_handed = src->left_handed;
	}
	if (src->middle_emulation != INT_MIN) {
		dst->middle_emulation = src->middle_emulation;
	}
	if (src->natural_scroll != INT_MIN) {
		dst->natural_scroll = src->natural_scroll;
	}
	if (src->pointer_accel != FLT_MIN) {
		dst->pointer_accel = src->pointer_accel;
	}
	if (src->sensitivity != FLT_MIN) {
		dst->sensitivity = src->sensitivity;
	}
	if (src->scroll_factor != FLT_MIN) {
		dst->scroll_factor = src->scroll_factor;
	}
	if (src->repeat_delay != INT_MIN) {
		dst->repeat_delay = src->repeat_delay;
	}
	if (src->repeat_rate != INT_MIN) {
		dst->repeat_rate = src->repeat_rate;
	}
	if (src->scroll_method != INT_MIN) {
		dst->scroll_method = src->scroll_method;
	}
	if (src->scroll_button != INT_MIN) {
		dst->scroll_button = src->scroll_button;
	}
	if (src->send_events != INT_MIN) {
		dst->send_events = src->send_events;
	}
	if (src->tap != INT_MIN) {
		dst->tap = src->tap;
	}
	if (src->tap_button_map != INT_MIN) {
		dst->tap_button_map = src->tap_button_map;
	}
	if (src->xkb_file_is_set) {
		free(dst->xkb_file);
		dst->xkb_file = src->xkb_file ? strdup(src->xkb_file) : NULL;
		dst->xkb_file_is_set = dst->xkb_file != NULL;
	}
	if (src->xkb_layout) {
		free(dst->xkb_layout);
		dst->xkb_layout = strdup(src->xkb_layout);
	}
	if (src->xkb_model) {
		free(dst->xkb_model);
		dst->xkb_model = strdup(src->xkb_model);
	}
	if (src->xkb_options) {
		free(dst->xkb_options);
		dst->xkb_options = strdup(src->xkb_options);
	}
	if (src->xkb_rules) {
		free(dst->xkb_rules);
		dst->xkb_rules = strdup(src->xkb_rules);
	}
	if (src->xkb_variant) {
		free(dst->xkb_variant);
		dst->xkb_variant = strdup(src->xkb_variant);
	}
	if (src->xkb_numlock != INT_MIN) {
		dst->xkb_numlock = src->xkb_numlock;
	}
	if (src->xkb_capslock != INT_MIN) {
		dst->xkb_capslock = src->xkb_capslock;
	}
	if (src->mapped_from_region) {
		free(dst->mapped_from_region);
		dst->mapped_from_region =
			malloc(sizeof(struct input_config_mapped_from_region));
		memcpy(dst->mapped_from_region, src->mapped_from_region,
			sizeof(struct input_config_mapped_from_region));
	}
	if (src->mapped_to) {
		dst->mapped_to = src->mapped_to;
	}
	if (src->mapped_to_output) {
		free(dst->mapped_to_output);
		dst->mapped_to_output = strdup(src->mapped_to_output);
	}
	if (src->mapped_to_region) {
		free(dst->mapped_to_region);
		dst->mapped_to_region =
			malloc(sizeof(struct wlr_box));
		memcpy(dst->mapped_to_region, src->mapped_to_region,
			sizeof(struct wlr_box));
	}
	if (src->calibration_matrix.configured) {
		dst->calibration_matrix.configured = src->calibration_matrix.configured;
		memcpy(dst->calibration_matrix.matrix, src->calibration_matrix.matrix,
			sizeof(src->calibration_matrix.matrix));
	}
	for (int i = 0; i < src->tools->length; i++) {
		struct input_config_tool *src_tool = src->tools->items[i];
		for (int j = 0; j < dst->tools->length; j++) {
			struct input_config_tool *dst_tool = dst->tools->items[j];
			if (src_tool->type == dst_tool->type) {
				dst_tool->mode = src_tool->mode;
				goto tool_merge_outer;
			}
		}

		struct input_config_tool *dst_tool = malloc(sizeof(*dst_tool));
		memcpy(dst_tool, src_tool, sizeof(*dst_tool));
		list_add(dst->tools, dst_tool);

		tool_merge_outer:;
	}
}

static bool validate_xkb_merge(struct input_config *dest,
		struct input_config *src, char **xkb_error) {
	struct input_config *temp = new_input_config("temp");
	if (dest) {
		merge_input_config(temp, dest);
	}
	merge_input_config(temp, src);

	struct xkb_keymap *keymap = sway_keyboard_compile_keymap(temp, xkb_error);
	free_input_config(temp);
	if (!keymap) {
		return false;
	}

	xkb_keymap_unref(keymap);
	return true;
}

static bool validate_wildcard_on_all(struct input_config *wildcard,
		char **error) {
	for (int i = 0; i < config->input_configs->length; i++) {
		struct input_config *ic = config->input_configs->items[i];
		if (strcmp(wildcard->identifier, ic->identifier) != 0) {
			sway_log(SWAY_DEBUG, "Validating xkb merge of * on %s",
					ic->identifier);
			if (!validate_xkb_merge(ic, wildcard, error)) {
				return false;
			}
		}
	}

	for (int i = 0; i < config->input_type_configs->length; i++) {
		struct input_config *ic = config->input_type_configs->items[i];
		sway_log(SWAY_DEBUG, "Validating xkb merge of * config on %s",
				ic->identifier);
		if (!validate_xkb_merge(ic, wildcard, error)) {
			return false;
		}
	}

	return true;
}

static void merge_wildcard_on_all(struct input_config *wildcard) {
	for (int i = 0; i < config->input_configs->length; i++) {
		struct input_config *ic = config->input_configs->items[i];
		if (strcmp(wildcard->identifier, ic->identifier) != 0) {
			sway_log(SWAY_DEBUG, "Merging input * config on %s", ic->identifier);
			merge_input_config(ic, wildcard);
		}
	}

	for (int i = 0; i < config->input_type_configs->length; i++) {
		struct input_config *ic = config->input_type_configs->items[i];
		sway_log(SWAY_DEBUG, "Merging input * config on %s", ic->identifier);
		merge_input_config(ic, wildcard);
	}
}

static bool validate_type_on_existing(struct input_config *type_wildcard,
		char **error) {
	for (int i = 0; i < config->input_configs->length; i++) {
		struct input_config *ic = config->input_configs->items[i];
		if (ic->input_type == NULL) {
			continue;
		}

		if (strcmp(ic->input_type, type_wildcard->identifier + 5) == 0) {
			sway_log(SWAY_DEBUG, "Validating merge of %s on %s",
				type_wildcard->identifier, ic->identifier);
			if (!validate_xkb_merge(ic, type_wildcard, error)) {
				return false;
			}
		}
	}
	return true;
}

static void merge_type_on_existing(struct input_config *type_wildcard) {
	for (int i = 0; i < config->input_configs->length; i++) {
		struct input_config *ic = config->input_configs->items[i];
		if (ic->input_type == NULL) {
			continue;
		}

		if (strcmp(ic->input_type, type_wildcard->identifier + 5) == 0) {
			sway_log(SWAY_DEBUG, "Merging %s top of %s",
				type_wildcard->identifier,
				ic->identifier);
			merge_input_config(ic, type_wildcard);
		}
	}
}

static const char *set_input_type(struct input_config *ic) {
	struct sway_input_device *input_device;
	wl_list_for_each(input_device, &server.input->devices, link) {
		if (strcmp(input_device->identifier, ic->identifier) == 0) {
			ic->input_type = input_device_get_type(input_device);
			break;
		}
	}
	return ic->input_type;
}

struct input_config *store_input_config(struct input_config *ic,
		char **error) {
	bool wildcard = strcmp(ic->identifier, "*") == 0;
	if (wildcard && error && !validate_wildcard_on_all(ic, error)) {
		return NULL;
	}

	bool type = strncmp(ic->identifier, "type:", strlen("type:")) == 0;
	if (type && error && !validate_type_on_existing(ic, error)) {
		return NULL;
	}

	list_t *config_list = type ? config->input_type_configs
		: config->input_configs;

	struct input_config *current = NULL;
	bool new_current = false;

	int i = list_seq_find(config_list, input_identifier_cmp, ic->identifier);
	if (i >= 0) {
		current = config_list->items[i];
	}

	if (!current && !wildcard && !type && set_input_type(ic)) {
		for (i = 0; i < config->input_type_configs->length; i++) {
			struct input_config *tc = config->input_type_configs->items[i];
			if (strcmp(ic->input_type, tc->identifier + 5) == 0) {
				current = new_input_config(ic->identifier);
				current->input_type = ic->input_type;
				merge_input_config(current, tc);
				new_current = true;
				break;
			}
		}
	}

	i = list_seq_find(config->input_configs, input_identifier_cmp, "*");
	if (!current && i >= 0) {
		current = new_input_config(ic->identifier);
		merge_input_config(current, config->input_configs->items[i]);
		new_current = true;
	}

	if (error && !validate_xkb_merge(current, ic, error)) {
		if (new_current) {
			free_input_config(current);
		}
		return NULL;
	}

	if (wildcard) {
		merge_wildcard_on_all(ic);
	}

	if (type) {
		merge_type_on_existing(ic);
	}

	if (current) {
		merge_input_config(current, ic);
		free_input_config(ic);
		ic = current;
	}

	ic->xkb_file_is_set = ic->xkb_file != NULL;

	if (!current || new_current) {
		list_add(config_list, ic);
	}

	sway_log(SWAY_DEBUG, "Config stored for input %s", ic->identifier);

	return ic;
}

void input_config_fill_rule_names(struct input_config *ic,
		struct xkb_rule_names *rules) {
	rules->layout = ic->xkb_layout;
	rules->model = ic->xkb_model;
	rules->options = ic->xkb_options;
	rules->rules = ic->xkb_rules;
	rules->variant = ic->xkb_variant;
}

void free_input_config(struct input_config *ic) {
	if (!ic) {
		return;
	}
	free(ic->identifier);
	free(ic->xkb_file);
	free(ic->xkb_layout);
	free(ic->xkb_model);
	free(ic->xkb_options);
	free(ic->xkb_rules);
	free(ic->xkb_variant);
	free(ic->mapped_from_region);
	free(ic->mapped_to_output);
	free(ic->mapped_to_region);
	list_free_items_and_destroy(ic->tools);
	free(ic);
}

int input_identifier_cmp(const void *item, const void *data) {
	const struct input_config *ic = item;
	const char *identifier = data;
	return strcmp(ic->identifier, identifier);
}
