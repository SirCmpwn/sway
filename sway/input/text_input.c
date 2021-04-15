#include <assert.h>
#include <stdlib.h>
#include "log.h"
#include "sway/input/seat.h"
#include "sway/input/text_input.h"

static struct sway_text_input *relay_get_focusable_text_input(
		struct sway_input_method_relay *relay) {
	struct sway_text_input *text_input = NULL;
	wl_list_for_each(text_input, &relay->text_inputs, link) {
		if (text_input->pending_focused_surface) {
			return text_input;
		}
	}
	return NULL;
}

static struct sway_text_input *relay_get_focused_text_input(
		struct sway_input_method_relay *relay) {
	struct sway_text_input *text_input = NULL;
	wl_list_for_each(text_input, &relay->text_inputs, link) {
		if (text_input->input->focused_surface) {
			return text_input;
		}
	}
	return NULL;
}

static void handle_im_commit(struct wl_listener *listener, void *data) {
	struct sway_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_commit);

	struct sway_text_input *text_input = relay_get_focused_text_input(relay);
	if (!text_input) {
		return;
	}
	struct wlr_input_method_v2 *context = data;
	assert(context == relay->input_method);
	if (context->current.preedit.text) {
		wlr_text_input_v3_send_preedit_string(text_input->input,
			context->current.preedit.text,
			context->current.preedit.cursor_begin,
			context->current.preedit.cursor_end);
	}
	if (context->current.commit_text) {
		wlr_text_input_v3_send_commit_string(text_input->input,
			context->current.commit_text);
	}
	if (context->current.delete.before_length
			|| context->current.delete.after_length) {
		wlr_text_input_v3_send_delete_surrounding_text(text_input->input,
			context->current.delete.before_length,
			context->current.delete.after_length);
	}
	wlr_text_input_v3_send_done(text_input->input);
}

static void handle_im_keyboard_grab_destroy(struct wl_listener *listener, void *data) {
	struct sway_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_keyboard_grab_destroy);
	struct wlr_input_method_keyboard_grab_v2 *keyboard_grab = data;
	wl_list_remove(&relay->input_method_keyboard_grab_destroy.link);

	if (keyboard_grab->keyboard) {
		// send modifier state to original client
		wlr_seat_keyboard_notify_modifiers(keyboard_grab->input_method->seat,
			&keyboard_grab->keyboard->modifiers);
	}
}

static void handle_im_grab_keyboard(struct wl_listener *listener, void *data) {
	struct sway_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_grab_keyboard);
	struct wlr_input_method_keyboard_grab_v2 *keyboard_grab = data;

	// send modifier state to grab
	struct wlr_keyboard *active_keyboard = wlr_seat_get_keyboard(relay->seat->wlr_seat);
	wlr_input_method_keyboard_grab_v2_set_keyboard(keyboard_grab,
		active_keyboard);
	wlr_input_method_keyboard_grab_v2_send_modifiers(keyboard_grab,
		&active_keyboard->modifiers);

	wl_signal_add(&keyboard_grab->events.destroy,
		&relay->input_method_keyboard_grab_destroy);
	relay->input_method_keyboard_grab_destroy.notify =
		handle_im_keyboard_grab_destroy;
}

static void text_input_set_pending_focused_surface(
		struct sway_text_input *text_input, struct wlr_surface *surface) {
	wl_list_remove(&text_input->pending_focused_surface_destroy.link);
	text_input->pending_focused_surface = surface;

	if (surface) {
		wl_signal_add(&surface->events.destroy,
			&text_input->pending_focused_surface_destroy);
	} else {
		wl_list_init(&text_input->pending_focused_surface_destroy.link);
	}
}

static void handle_im_destroy(struct wl_listener *listener, void *data) {
	struct sway_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_destroy);
	struct wlr_input_method_v2 *context = data;
	assert(context == relay->input_method);
	relay->input_method = NULL;
	struct sway_text_input *text_input = relay_get_focused_text_input(relay);
	if (text_input) {
		// keyboard focus is still there, so keep the surface at hand in case
		// the input method returns
		text_input_set_pending_focused_surface(text_input,
			text_input->input->focused_surface);
		wlr_text_input_v3_send_leave(text_input->input);
	}
}

static void relay_send_im_state(struct sway_input_method_relay *relay,
		struct wlr_text_input_v3 *input) {
	struct wlr_input_method_v2 *input_method = relay->input_method;
	if (!input_method) {
		sway_log(SWAY_INFO, "Sending IM_DONE but im is gone");
		return;
	}
	// TODO: only send each of those if they were modified
	if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
		wlr_input_method_v2_send_surrounding_text(input_method,
			input->current.surrounding.text, input->current.surrounding.cursor,
			input->current.surrounding.anchor);
	}
	wlr_input_method_v2_send_text_change_cause(input_method,
		input->current.text_change_cause);
	if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
		wlr_input_method_v2_send_content_type(input_method,
			input->current.content_type.hint,
			input->current.content_type.purpose);
	}
	wlr_input_method_v2_send_done(input_method);
	// TODO: pass intent, display popup size
}

static void handle_text_input_enable(struct wl_listener *listener, void *data) {
	struct sway_text_input *text_input = wl_container_of(listener, text_input,
		text_input_enable);
	if (text_input->relay->input_method == NULL) {
		sway_log(SWAY_INFO, "Enabling text input when input method is gone");
		return;
	}
	wlr_input_method_v2_send_activate(text_input->relay->input_method);
	relay_send_im_state(text_input->relay, text_input->input);
}

static void handle_text_input_commit(struct wl_listener *listener,
		void *data) {
	struct sway_text_input *text_input = wl_container_of(listener, text_input,
		text_input_commit);
	if (!text_input->input->current_enabled) {
		sway_log(SWAY_INFO, "Inactive text input tried to commit an update");
		return;
	}
	sway_log(SWAY_DEBUG, "Text input committed update");
	if (text_input->relay->input_method == NULL) {
		sway_log(SWAY_INFO, "Text input committed, but input method is gone");
		return;
	}
	relay_send_im_state(text_input->relay, text_input->input);
}

static void relay_disable_text_input(struct sway_input_method_relay *relay,
		struct sway_text_input *text_input) {
	if (relay->input_method == NULL) {
		sway_log(SWAY_DEBUG, "Disabling text input, but input method is gone");
		return;
	}
	wlr_input_method_v2_send_deactivate(relay->input_method);
	relay_send_im_state(relay, text_input->input);
}

static void handle_text_input_disable(struct wl_listener *listener,
		void *data) {
	struct sway_text_input *text_input = wl_container_of(listener, text_input,
		text_input_disable);
	if (text_input->input->focused_surface == NULL) {
		sway_log(SWAY_DEBUG, "Disabling text input, but no longer focused");
		return;
	}
	relay_disable_text_input(text_input->relay, text_input);
}

static void handle_text_input_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_text_input *text_input = wl_container_of(listener, text_input,
		text_input_destroy);

	if (text_input->input->current_enabled) {
		relay_disable_text_input(text_input->relay, text_input);
	}
	text_input_set_pending_focused_surface(text_input, NULL);
	wl_list_remove(&text_input->text_input_commit.link);
	wl_list_remove(&text_input->text_input_destroy.link);
	wl_list_remove(&text_input->text_input_disable.link);
	wl_list_remove(&text_input->text_input_enable.link);
	wl_list_remove(&text_input->link);
	free(text_input);
}

static void handle_pending_focused_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct sway_text_input *text_input = wl_container_of(listener, text_input,
		pending_focused_surface_destroy);
	struct wlr_surface *surface = data;
	assert(text_input->pending_focused_surface == surface);
	text_input->pending_focused_surface = NULL;
	wl_list_remove(&text_input->pending_focused_surface_destroy.link);
	wl_list_init(&text_input->pending_focused_surface_destroy.link);
}

struct sway_text_input *sway_text_input_create(
		struct sway_input_method_relay *relay,
		struct wlr_text_input_v3 *text_input) {
	struct sway_text_input *input = calloc(1, sizeof(struct sway_text_input));
	if (!input) {
		return NULL;
	}
	input->input = text_input;
	input->relay = relay;

	wl_list_insert(&relay->text_inputs, &input->link);

	input->text_input_enable.notify = handle_text_input_enable;
	wl_signal_add(&text_input->events.enable, &input->text_input_enable);

	input->text_input_commit.notify = handle_text_input_commit;
	wl_signal_add(&text_input->events.commit, &input->text_input_commit);

	input->text_input_disable.notify = handle_text_input_disable;
	wl_signal_add(&text_input->events.disable, &input->text_input_disable);

	input->text_input_destroy.notify = handle_text_input_destroy;
	wl_signal_add(&text_input->events.destroy, &input->text_input_destroy);

	input->pending_focused_surface_destroy.notify =
		handle_pending_focused_surface_destroy;
	wl_list_init(&input->pending_focused_surface_destroy.link);
	return input;
}

static void relay_handle_text_input(struct wl_listener *listener,
		void *data) {
	struct sway_input_method_relay *relay = wl_container_of(listener, relay,
		text_input_new);
	struct wlr_text_input_v3 *wlr_text_input = data;
	if (relay->seat->wlr_seat != wlr_text_input->seat) {
		return;
	}

	sway_text_input_create(relay, wlr_text_input);
}

static void relay_handle_input_method(struct wl_listener *listener,
		void *data) {
	struct sway_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_new);
	struct wlr_input_method_v2 *input_method = data;
	if (relay->seat->wlr_seat != input_method->seat) {
		return;
	}

	if (relay->input_method != NULL) {
		sway_log(SWAY_INFO, "Attempted to connect second input method to a seat");
		wlr_input_method_v2_send_unavailable(input_method);
		return;
	}

	relay->input_method = input_method;
	wl_signal_add(&relay->input_method->events.commit,
		&relay->input_method_commit);
	relay->input_method_commit.notify = handle_im_commit;
	wl_signal_add(&relay->input_method->events.grab_keyboard,
		&relay->input_method_grab_keyboard);
	relay->input_method_grab_keyboard.notify = handle_im_grab_keyboard;
	wl_signal_add(&relay->input_method->events.destroy,
		&relay->input_method_destroy);
	relay->input_method_destroy.notify = handle_im_destroy;

	struct sway_text_input *text_input = relay_get_focusable_text_input(relay);
	if (text_input) {
		wlr_text_input_v3_send_enter(text_input->input,
			text_input->pending_focused_surface);
		text_input_set_pending_focused_surface(text_input, NULL);
	}
}

void sway_input_method_relay_init(struct sway_seat *seat,
		struct sway_input_method_relay *relay) {
	relay->seat = seat;
	wl_list_init(&relay->text_inputs);

	relay->text_input_new.notify = relay_handle_text_input;
	wl_signal_add(&server.text_input->events.text_input,
		&relay->text_input_new);

	relay->input_method_new.notify = relay_handle_input_method;
	wl_signal_add(
		&server.input_method->events.input_method,
		&relay->input_method_new);
}

void sway_input_method_relay_finish(struct sway_input_method_relay *relay) {
	wl_list_remove(&relay->input_method_new.link);
	wl_list_remove(&relay->text_input_new.link);
}

void sway_input_method_relay_set_focus(struct sway_input_method_relay *relay,
		struct wlr_surface *surface) {
	struct sway_text_input *text_input;
	wl_list_for_each(text_input, &relay->text_inputs, link) {
		if (text_input->pending_focused_surface) {
			assert(text_input->input->focused_surface == NULL);
			if (surface != text_input->pending_focused_surface) {
				text_input_set_pending_focused_surface(text_input, NULL);
			}
		} else if (text_input->input->focused_surface) {
			assert(text_input->pending_focused_surface == NULL);
			if (surface != text_input->input->focused_surface) {
				relay_disable_text_input(relay, text_input);
				wlr_text_input_v3_send_leave(text_input->input);
			} else {
				sway_log(SWAY_DEBUG, "IM relay set_focus already focused");
				continue;
			}
		}

		if (surface
				&& wl_resource_get_client(text_input->input->resource)
				== wl_resource_get_client(surface->resource)) {
			if (relay->input_method) {
				wlr_text_input_v3_send_enter(text_input->input, surface);
			} else {
				text_input_set_pending_focused_surface(text_input, surface);
			}
		}
	}
}
