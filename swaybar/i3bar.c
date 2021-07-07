#define _POSIX_C_SOURCE 200809L
#include <json.h>
#include <linux/input-event-codes.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "log.h"
#include "swaybar/bar.h"
#include "swaybar/config.h"
#include "swaybar/i3bar.h"
#include "swaybar/input.h"
#include "swaybar/status_line.h"

void i3bar_block_unref(struct i3bar_block *block) {
	if (block == NULL) {
		return;
	}

	if (--block->ref_count == 0) {
		free(block->full_text);
		free(block->short_text);
		free(block->align);
		free(block->min_width_str);
		free(block->name);
		free(block->instance);
		free(block);
	}
}

static void i3bar_parse_json(struct status_line *status,
		struct json_object *json_array) {
	struct i3bar_block *block, *tmp;
	wl_list_for_each_safe(block, tmp, &status->blocks, link) {
		wl_list_remove(&block->link);
		i3bar_block_unref(block);
	}
	for (size_t i = 0; i < json_object_array_length(json_array); ++i) {
		json_object *full_text, *short_text, *color, *min_width, *align, *urgent;
		json_object *name, *instance, *separator, *separator_block_width;
		json_object *background, *border, *border_top, *border_bottom;
		json_object *border_left, *border_right, *markup;
		json_object *json = json_object_array_get_idx(json_array, i);
		if (!json) {
			continue;
		}
		json_object_object_get_ex(json, "full_text", &full_text);
		json_object_object_get_ex(json, "short_text", &short_text);
		json_object_object_get_ex(json, "color", &color);
		json_object_object_get_ex(json, "min_width", &min_width);
		json_object_object_get_ex(json, "align", &align);
		json_object_object_get_ex(json, "urgent", &urgent);
		json_object_object_get_ex(json, "name", &name);
		json_object_object_get_ex(json, "instance", &instance);
		json_object_object_get_ex(json, "markup", &markup);
		json_object_object_get_ex(json, "separator", &separator);
		json_object_object_get_ex(json, "separator_block_width", &separator_block_width);
		json_object_object_get_ex(json, "background", &background);
		json_object_object_get_ex(json, "border", &border);
		json_object_object_get_ex(json, "border_top", &border_top);
		json_object_object_get_ex(json, "border_bottom", &border_bottom);
		json_object_object_get_ex(json, "border_left", &border_left);
		json_object_object_get_ex(json, "border_right", &border_right);

		struct i3bar_block *block = calloc(1, sizeof(struct i3bar_block));
		block->ref_count = 1;
		block->full_text = full_text ?
			strdup(json_object_get_string(full_text)) : NULL;
		block->short_text = short_text ?
			strdup(json_object_get_string(short_text)) : NULL;
		if (color) {
			const char *hexstring = json_object_get_string(color);
			block->color_set = parse_color(hexstring, &block->color);
			if (!block->color_set) {
				sway_log(SWAY_ERROR, "Invalid block color: %s", hexstring);
			}
		}
		if (min_width) {
			json_type type = json_object_get_type(min_width);
			if (type == json_type_int) {
				block->min_width = json_object_get_int(min_width);
			} else if (type == json_type_string) {
				/* the width will be calculated when rendering */
				block->min_width_str = strdup(json_object_get_string(min_width));
			}
		}
		block->align = strdup(align ? json_object_get_string(align) : "left");
		block->urgent = urgent ? json_object_get_int(urgent) : false;
		block->name = name ? strdup(json_object_get_string(name)) : NULL;
		block->instance = instance ?
			strdup(json_object_get_string(instance)) : NULL;
		if (markup) {
			block->markup = false;
			const char *markup_str = json_object_get_string(markup);
			if (strcmp(markup_str, "pango") == 0) {
				block->markup = true;
			}
		}
		block->separator = separator ? json_object_get_int(separator) : true;
		block->separator_block_width = separator_block_width ?
			json_object_get_int(separator_block_width) : 9;
		// Airblader features
		const char *hex = background ? json_object_get_string(background) : NULL;
		if (hex && !parse_color(hex, &block->background)) {
			sway_log(SWAY_ERROR, "Ignoring invalid block background: %s", hex);
		}
		hex = border ? json_object_get_string(border) : NULL;
		if (hex && !parse_color(hex, &block->border)) {
			sway_log(SWAY_ERROR, "Ignoring invalid block border: %s", hex);
		}
		block->border_top = border_top ? json_object_get_int(border_top) : 1;
		block->border_bottom = border_bottom ?
			json_object_get_int(border_bottom) : 1;
		block->border_left = border_left ? json_object_get_int(border_left) : 1;
		block->border_right = border_right ?
			json_object_get_int(border_right) : 1;
		wl_list_insert(&status->blocks, &block->link);
	}
}

bool i3bar_handle_readable(struct status_line *status) {
	while (!status->started) { // look for opening bracket
		for (size_t c = 0; c < status->buffer_index; ++c) {
			if (status->buffer[c] == '[') {
				status->started = true;
				status->buffer_index -= ++c;
				memmove(status->buffer, &status->buffer[c], status->buffer_index);
				break;
			} else if (!isspace(status->buffer[c])) {
				sway_log(SWAY_DEBUG, "Invalid i3bar json: expected '[' but encountered '%c'",
						status->buffer[c]);
				status_error(status, "[invalid i3bar json]");
				return true;
			}
		}
		if (status->started) {
			break;
		}

		errno = 0;
		ssize_t read_bytes = read(status->read_fd, status->buffer, status->buffer_size);
		if (read_bytes > -1) {
			status->buffer_index = read_bytes;
		} else if (errno == EAGAIN) {
			return false;
		} else {
			status_error(status, "[error reading from status command]");
			return true;
		}
	}

	struct json_object *last_object = NULL;
	struct json_object *test_object;
	size_t buffer_pos = 0;
	while (true) {
		// since the incoming stream is an infinite array
		// parsing is split into two parts
		// first, attempt to parse the current object, reading more if the
		// parser indicates that the current object is incomplete, and failing
		// if the parser fails
		// second, look for separating comma, ignoring whitespace, failing if
		// any other characters are encountered
		if (status->expecting_comma) {
			for (; buffer_pos < status->buffer_index; ++buffer_pos) {
				if (status->buffer[buffer_pos] == ',') {
					status->expecting_comma = false;
					++buffer_pos;
					break;
				} else if (!isspace(status->buffer[buffer_pos])) {
					sway_log(SWAY_DEBUG, "Invalid i3bar json: expected ',' but encountered '%c'",
							status->buffer[buffer_pos]);
					status_error(status, "[invalid i3bar json]");
					return true;
				}
			}
			if (buffer_pos < status->buffer_index) {
				continue; // look for new object without reading more input
			}
			buffer_pos = status->buffer_index = 0;
		} else {
			test_object = json_tokener_parse_ex(status->tokener,
					&status->buffer[buffer_pos], status->buffer_index - buffer_pos);
			enum json_tokener_error err = json_tokener_get_error(status->tokener);
			if (err == json_tokener_success) {
				if (json_object_get_type(test_object) == json_type_array) {
					if (last_object) {
						json_object_put(last_object);
					}
					last_object = test_object;
				} else {
					json_object_put(test_object);
				}

				// in order to print the json for debugging purposes
				// the last character is temporarily replaced with a null character
				// (the last character is used in case the buffer is full)
				char *last_char_pos =
					&status->buffer[buffer_pos + status->tokener->char_offset - 1];
				char last_char = *last_char_pos;
				while (isspace(last_char)) {
					last_char = *--last_char_pos;
				}
				*last_char_pos = '\0';
				size_t offset = strspn(&status->buffer[buffer_pos], " \f\n\r\t\v");
				sway_log(SWAY_DEBUG, "Received i3bar json: '%s%c'",
						&status->buffer[buffer_pos + offset], last_char);
				*last_char_pos = last_char;

				buffer_pos += status->tokener->char_offset;
				status->expecting_comma = true;

				if (buffer_pos < status->buffer_index) {
					continue; // look for comma without reading more input
				}
				buffer_pos = status->buffer_index = 0;
			} else if (err == json_tokener_continue) {
				json_tokener_reset(status->tokener);
				if (status->buffer_index < status->buffer_size) {
					// move the object to the start of the buffer
					status->buffer_index -= buffer_pos;
					memmove(status->buffer, &status->buffer[buffer_pos],
							status->buffer_index);
					buffer_pos = 0;
				} else {
					// expand buffer
					status->buffer_size *= 2;
					char *new_buffer = realloc(status->buffer, status->buffer_size);
					if (new_buffer) {
						status->buffer = new_buffer;
					} else {
						free(status->buffer);
						status_error(status, "[failed to allocate buffer]");
						return true;
					}
				}
			} else {
				char last_char = status->buffer[status->buffer_index - 1];
				status->buffer[status->buffer_index - 1] = '\0';
				sway_log(SWAY_DEBUG, "Failed to parse i3bar json - %s: '%s%c'",
						json_tokener_error_desc(err), &status->buffer[buffer_pos], last_char);
				status_error(status, "[failed to parse i3bar json]");
				return true;
			}
		}

		errno = 0;
		ssize_t read_bytes = read(status->read_fd, &status->buffer[status->buffer_index],
				status->buffer_size - status->buffer_index);
		if (read_bytes > -1) {
			status->buffer_index += read_bytes;
		} else if (errno == EAGAIN) {
			break;
		} else {
			status_error(status, "[error reading from status command]");
			return true;
		}
	}

	if (last_object) {
		sway_log(SWAY_DEBUG, "Rendering last received json");
		i3bar_parse_json(status, last_object);
		json_object_put(last_object);
		return true;
	} else {
		return false;
	}
}

enum hotspot_event_handling i3bar_block_send_click(struct status_line *status,
		struct i3bar_block *block, double x, double y, double rx, double ry,
		double w, double h, int scale, uint32_t button) {
	sway_log(SWAY_DEBUG, "block %s clicked", block->name);
	if (!block->name || !status->click_events) {
		return HOTSPOT_PROCESS;
	}

	struct json_object *event_json = json_object_new_object();
	json_object_object_add(event_json, "name",
			json_object_new_string(block->name));
	if (block->instance) {
		json_object_object_add(event_json, "instance",
				json_object_new_string(block->instance));
	}

	json_object_object_add(event_json, "button",
			json_object_new_int(event_to_x11_button(button)));

	struct json_object *modifiers = json_object_new_array();
	// char mods[16][16];
	// int len = get_modifier_names(mods, button);
	// while (len--)
	// 	json_object_array_add(modifiers, json_object_new_string(mods[len]));

	json_object_object_add(event_json, "modifiers", modifiers);
	json_object_object_add(event_json, "event", json_object_new_int(button));
	if (status->float_event_coords) {
		json_object_object_add(event_json, "x", json_object_new_double(x));
		json_object_object_add(event_json, "y", json_object_new_double(y));
		json_object_object_add(event_json, "relative_x", json_object_new_double(rx));
		json_object_object_add(event_json, "relative_y", json_object_new_double(ry));
		json_object_object_add(event_json, "width", json_object_new_double(w));
		json_object_object_add(event_json, "height", json_object_new_double(h));
	} else {
		json_object_object_add(event_json, "x", json_object_new_int(x));
		json_object_object_add(event_json, "y", json_object_new_int(y));
		json_object_object_add(event_json, "relative_x", json_object_new_int(rx));
		json_object_object_add(event_json, "relative_y", json_object_new_int(ry));
		json_object_object_add(event_json, "width", json_object_new_int(w));
		json_object_object_add(event_json, "height", json_object_new_int(h));
	}
	json_object_object_add(event_json, "scale", json_object_new_int(scale));
	if (dprintf(status->write_fd, "%s%s\n", status->clicked ? "," : "",
				json_object_to_json_string(event_json)) < 0) {
		status_error(status, "[failed to write click event]");
	}
	status->clicked = true;
	json_object_put(event_json);
	return HOTSPOT_IGNORE;
}
