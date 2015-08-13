#include <xkbcommon/xkbcommon.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wlc/wlc.h>
#include <ctype.h>
#include "layout.h"
#include "log.h"
#include "config.h"
#include "commands.h"
#include "handlers.h"

static bool handle_output_created(wlc_handle output) {
	add_output(output);
	return true;
}

static void handle_output_destroyed(wlc_handle output) {
	destroy_output(output);
}

static void handle_output_resolution_change(wlc_handle output, const struct wlc_size *from, const struct wlc_size *to) {
	sway_log(L_DEBUG, "Output %d resolution changed to %d x %d", output, to->w, to->h);
	swayc_t *c = get_swayc_for_handle(output, &root_container);
	if (!c) return;
	c->width = to->w;
	c->height = to->h;
	arrange_windows(&root_container, -1, -1);
}

static void handle_output_focused(wlc_handle output, bool focus) {
	swayc_t *c = get_swayc_for_handle(output, &root_container);
	if (!c) return;
	if (focus) {
		unfocus_all(&root_container);
		focus_view(c);
	}
}

static bool handle_view_created(wlc_handle view) {
	add_view(view);
	return true;
}

static void handle_view_destroyed(wlc_handle view) {
	sway_log(L_DEBUG, "Destroying window %d", view);
	destroy_view(get_swayc_for_handle(view, &root_container));
}

static void handle_view_focus(wlc_handle view, bool focus) {
	return;
}

static void handle_view_geometry_request(wlc_handle view, const struct wlc_geometry* geometry) {
	// deny that shit
}


static bool handle_key(wlc_handle view, uint32_t time, const struct wlc_modifiers
		*modifiers, uint32_t key, uint32_t sym, enum wlc_key_state state) {
	enum { QSIZE = 32 };
	static uint8_t  head = 0;
	static uint32_t array[QSIZE];
	bool cmd_success = true;

	struct sway_mode *mode = config->current_mode;
	// Lowercase if necessary
	sym = tolower(sym);

	//Find key, if it has been pressed
	int mid = 0;
	while (mid < head && array[mid] != sym) {
		++mid;
	}
	if (state == WLC_KEY_STATE_PRESSED && mid == head && head + 1 < QSIZE) {
		array[head++] = sym;
	} else if (state == WLC_KEY_STATE_RELEASED && mid < head) {
		memmove(array + mid, array + mid + 1, sizeof*array * (--head - mid));
	}
	// TODO: reminder to check conflicts with mod+q+a versus mod+q
	int i;
	for (i = 0; i < mode->bindings->length; ++i) {
		struct sway_binding *binding = mode->bindings->items[i];

		if ((modifiers->mods & binding->modifiers) == binding->modifiers) {
			bool match;
			int j;
			for (j = 0; j < binding->keys->length; ++j) {
				match = false;
				xkb_keysym_t *key = binding->keys->items[j];
				uint8_t k;
				for (k = 0; k < head; ++k) {
					if (array[k] == *key) {
						match = true;
						break;
					}
				}
				if (match == false) {
					break;
				}
			}

			if (match) {
				//Remove matched keys from array
				int j;
				for (j = 0; j < binding->keys->length; ++j) {
					uint8_t k;
					for (k = 0; k < head; ++k) {
						memmove(array + k, array + k + 1, sizeof*array * (--head - k));
						break;
					}
				}
				if (state == WLC_KEY_STATE_PRESSED) {
					cmd_success = !handle_command(config, binding->command);
				} else if (state == WLC_KEY_STATE_RELEASED) {
					// TODO: --released
				}
			}
		}
	}
	return cmd_success;
}

bool pointer_test(swayc_t *view, void *_origin) {
	const struct wlc_origin *origin = _origin;
	if (view->type == C_VIEW && origin->x >= view->x && origin->y >= view->y
			&& origin->x < view->x + view->width && origin->y < view->y + view->height
			&& view->visible) {
		return true;
	}
	return false;
}

struct wlc_origin mouse_origin;

static bool handle_pointer_motion(wlc_handle view, uint32_t time, const struct wlc_origin *origin) {
	mouse_origin = *origin;
	if (!config->focus_follows_mouse) {
		return true;
	}
	swayc_t *c = find_container(&root_container, pointer_test, (void *)origin);
	swayc_t *focused = get_focused_container(&root_container);
	if (c && c != focused) {
		sway_log(L_DEBUG, "Switching focus to %p", c);
		unfocus_all(&root_container);
		focus_view(c);
	}
	return true;
}

static bool handle_pointer_button(wlc_handle view, uint32_t time, const struct wlc_modifiers *modifiers,
		uint32_t button, enum wlc_button_state state) {
	if (state == WLC_BUTTON_STATE_PRESSED) {
		swayc_t *c = find_container(&root_container, pointer_test, &mouse_origin);
		swayc_t *focused = get_focused_container(&root_container);
		if (c && c != focused) {
			sway_log(L_DEBUG, "Switching focus to %p", c);
			unfocus_all(&root_container);
			focus_view(c);
			return false;
		}
		return true;
	}
	return true;
}

static void handle_wlc_ready(void) {
	sway_log(L_DEBUG, "Compositor is ready, executing cmds in queue");
	int i;
	for (i = 0; i < config->cmd_queue->length; ++i) {
		sway_log(L_DEBUG, "Handling command %s", config->cmd_queue->items[i]);
		handle_command(config, config->cmd_queue->items[i]);
	}
	list_free(config->cmd_queue);
}


struct wlc_interface interface = {
	.output = {
		.created = handle_output_created,
		.destroyed = handle_output_destroyed,
		.resolution = handle_output_resolution_change,
		.focus = handle_output_focused
	},
	.view = {
		.created = handle_view_created,
		.destroyed = handle_view_destroyed,
		.focus = handle_view_focus,
		.request = {
			.geometry = handle_view_geometry_request
		}
	},
	.keyboard = {
		.key = handle_key
	},
	.pointer = {
		.motion = handle_pointer_motion,
		.button = handle_pointer_button
	},
	.compositor = {
		.ready = handle_wlc_ready 
	}
};

