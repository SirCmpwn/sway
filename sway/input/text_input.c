#include <assert.h>
#include <stdlib.h>
#include "log.h"
#include "sway/input/seat.h"
#include "sway/input/text_input.h"
#include "sway/tree/view.h"
#include "sway/tree/container.h"
#include "sway/desktop.h"
#include "sway/output.h"
#include "sway/layers.h"

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

static void input_popup_damage(struct sway_input_popup *popup) {
	if (!popup->popup_surface->mapped || !popup->visible) {
		return;
	}

	struct sway_text_input *text_input =
		relay_get_focused_text_input(popup->relay);
	if (text_input == NULL || text_input->input->focused_surface == NULL) {
		return;
	}

	struct wlr_surface *focused_surface = text_input->input->focused_surface;
	if (wlr_surface_is_layer_surface(focused_surface)) {
		struct wlr_layer_surface_v1 *layer_surface =
			wlr_layer_surface_v1_from_wlr_surface(focused_surface);
		struct sway_layer_surface *layer =
			layer_from_wlr_layer_surface_v1(layer_surface);
		output_damage_surface(layer->layer_surface->output->data,
			layer->geo.x + popup->x, layer->geo.y + popup->y,
			popup->popup_surface->surface, true);
		return;
	}

	struct sway_view *view = view_from_wlr_surface(
		text_input->input->focused_surface);
	desktop_damage_surface(popup->popup_surface->surface,
		view->container->surface_x - view->geometry.x + popup->x,
		view->container->surface_y - view->geometry.y + popup->y, true);
}

static void input_popup_update(struct sway_input_popup *popup) {
	input_popup_damage(popup);

	struct sway_text_input *text_input =
		relay_get_focused_text_input(popup->relay);

	if (text_input == NULL || text_input->input->focused_surface == NULL) {
		return;
	}

	if (!popup->popup_surface->mapped) {
		return;
	}

	bool cursor_rect = text_input->input->current.features
		& WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE;
	struct wlr_surface *focused_surface = text_input->input->focused_surface;
	struct wlr_box cursor = text_input->input->current.cursor_rectangle;

	struct wlr_output *output;
	struct wlr_box *output_box;
	struct wlr_box parent;
	if (wlr_surface_is_layer_surface(focused_surface)) {
		struct wlr_layer_surface_v1 *layer_surface =
			wlr_layer_surface_v1_from_wlr_surface(focused_surface);
		struct sway_layer_surface *layer =
			layer_from_wlr_layer_surface_v1(layer_surface);
		output = layer->layer_surface->output;
		output_box = wlr_output_layout_get_box(root->output_layout, output);
		parent = layer->geo;
		parent.x += output_box->x;
		parent.y += output_box->y;
	} else {
		struct sway_view *view = view_from_wlr_surface(focused_surface);
		output = wlr_output_layout_output_at(root->output_layout,
			view->container->surface_x + view->geometry.x,
			view->container->surface_y + view->geometry.y);
		output_box = wlr_output_layout_get_box(root->output_layout, output);
		parent.x = view->container->surface_x + view->geometry.x;
		parent.y = view->container->surface_y + view->geometry.y;
		parent.width = view->geometry.width;
		parent.height = view->geometry.height;
	}

	if (!cursor_rect) {
		cursor.x = 0;
		cursor.y = 0;
		cursor.width = parent.width;
		cursor.height = parent.height;
	}

	int popup_width = popup->popup_surface->surface->current.width;
	int popup_height = popup->popup_surface->surface->current.height;
	int x1 = parent.x + cursor.x;
	int x2 = parent.x + cursor.x + cursor.width;
	int y1 = parent.y + cursor.y;
	int y2 = parent.y + cursor.y + cursor.height;
	int x = x1;
	int y = y2;

	int available_right = output_box->x + output_box->width - x1;
	int available_left = x2 - output_box->x;
	if (available_right < popup_width && available_left > available_right) {
		x = x2 - popup_width;
	}

	int available_down = output_box->y + output_box->height - y2;
	int available_up = y1 - output_box->y;
	if (available_down < popup_height && available_up > available_down) {
		y = y1 - popup_height;
	}

	popup->x = x - parent.x;
	popup->y = y - parent.y;

	// Hide popup if cursor position is completely out of bounds
	bool x1_in_bounds = (cursor.x >= 0 && cursor.x < parent.width);
	bool y1_in_bounds = (cursor.y >= 0 && cursor.y < parent.height);
	bool x2_in_bounds = (cursor.x + cursor.width >= 0
		&& cursor.x + cursor.width < parent.width);
	bool y2_in_bounds = (cursor.y + cursor.height >= 0
		&& cursor.y + cursor.height < parent.height);
	popup->visible =
		(x1_in_bounds && y1_in_bounds) || (x2_in_bounds && y2_in_bounds);

	if (cursor_rect) {
		struct wlr_box box = {
			.x = x1 - x,
			.y = y1 - y,
			.width = cursor.width,
			.height = cursor.height,
		};
		wlr_input_popup_surface_v2_send_text_input_rectangle(
			popup->popup_surface, &box);
	}

	input_popup_damage(popup);
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_leave(surface, wlr_output);
}

static void input_popup_send_outputs(struct sway_input_popup *popup,
		wlr_surface_iterator_func_t iterator) {
	struct sway_text_input *text_input =
		relay_get_focused_text_input(popup->relay);
	if (text_input == NULL || text_input->input->focused_surface == NULL) {
		return;
	}
	struct wlr_surface *focused_surface = text_input->input->focused_surface;
	if (wlr_surface_is_layer_surface(focused_surface)) {
		struct wlr_layer_surface_v1 *layer_surface =
			wlr_layer_surface_v1_from_wlr_surface(focused_surface);
		struct sway_layer_surface *layer =
			layer_from_wlr_layer_surface_v1(layer_surface);
		wlr_surface_for_each_surface(popup->popup_surface->surface,
			iterator, layer->layer_surface->output);
		return;
	}
	struct sway_view *view = view_from_wlr_surface(focused_surface);
	for (int i = 0; i < view->container->outputs->length; i++) {
		struct sway_output *output = view->container->outputs->items[i];
		wlr_surface_for_each_surface(popup->popup_surface->surface,
			iterator, output->wlr_output);
	}
}

static void handle_im_popup_map(struct wl_listener *listener, void *data) {
	struct sway_input_popup *popup =
		wl_container_of(listener, popup, popup_map);
	input_popup_send_outputs(popup, surface_send_enter_iterator);
	input_popup_update(popup);
}

static void handle_im_popup_unmap(struct wl_listener *listener, void *data) {
	struct sway_input_popup *popup =
		wl_container_of(listener, popup, popup_unmap);
	input_popup_send_outputs(popup, surface_send_leave_iterator);
	input_popup_update(popup);
}

static void handle_im_popup_destroy(struct wl_listener *listener, void *data) {
	struct sway_input_popup *popup =
		wl_container_of(listener, popup, popup_destroy);
	wl_list_remove(&popup->focused_surface_unmap.link);
	wl_list_remove(&popup->popup_surface_commit.link);
	wl_list_remove(&popup->popup_destroy.link);
	wl_list_remove(&popup->popup_unmap.link);
	wl_list_remove(&popup->popup_map.link);
	wl_list_remove(&popup->view_link);
	wl_list_remove(&popup->link);
	free(popup);
}

static void handle_im_popup_surface_commit(struct wl_listener *listener,
		void *data) {
	struct sway_input_popup *popup =
		wl_container_of(listener, popup, popup_surface_commit);
	input_popup_update(popup);
}

static void handle_im_focused_surface_destroy(
		struct wl_listener *listener, void *data) {
	struct sway_input_popup *popup =
		wl_container_of(listener, popup, focused_surface_unmap);
	wl_list_remove(&popup->view_link);
	wl_list_init(&popup->view_link);
	input_popup_update(popup);
}

static void input_popup_set_focus(struct sway_input_popup *popup,
		struct wlr_surface *surface) {
	wl_list_remove(&popup->view_link);
	wl_list_remove(&popup->focused_surface_unmap.link);

	if (surface == NULL) {
		wl_list_init(&popup->view_link);
		wl_list_init(&popup->focused_surface_unmap.link);
		input_popup_update(popup);
		return;
	}

	if (wlr_surface_is_layer_surface(surface)) {
		struct wlr_layer_surface_v1 *layer_surface =
			wlr_layer_surface_v1_from_wlr_surface(surface);
		struct sway_layer_surface *layer =
			layer_from_wlr_layer_surface_v1(layer_surface);
		wl_list_insert(&layer->input_popups, &popup->view_link);
		wl_signal_add(
			&layer->layer_surface->events.unmap, &popup->focused_surface_unmap);
		input_popup_update(popup);
		return;
	}

	struct sway_view *view = view_from_wlr_surface(surface);
	wl_list_insert(&view->input_popups, &popup->view_link);
	wl_signal_add(&view->events.unmap, &popup->focused_surface_unmap);
	input_popup_update(popup);
}

static void handle_im_new_popup_surface(struct wl_listener *listener,
		void *data) {
	struct sway_input_method_relay *relay = wl_container_of(listener, relay,
		input_method_new_popup_surface);
	struct sway_input_popup *popup = calloc(1, sizeof(*popup));
	popup->relay = relay;
	popup->popup_surface = data;
	popup->popup_surface->data = popup;

	wl_list_init(&popup->view_link);

	wl_signal_add(&popup->popup_surface->events.map, &popup->popup_map);
	popup->popup_map.notify = handle_im_popup_map;
	wl_signal_add(
		&popup->popup_surface->events.unmap, &popup->popup_unmap);
	popup->popup_unmap.notify = handle_im_popup_unmap;
	wl_signal_add(
		&popup->popup_surface->events.destroy, &popup->popup_destroy);
	popup->popup_destroy.notify = handle_im_popup_destroy;
	wl_signal_add(&popup->popup_surface->surface->events.commit,
		&popup->popup_surface_commit);
	popup->popup_surface_commit.notify = handle_im_popup_surface_commit;
	wl_list_init(&popup->focused_surface_unmap.link);
	popup->focused_surface_unmap.notify = handle_im_focused_surface_destroy;

	struct sway_text_input *text_input = relay_get_focused_text_input(relay);
	if (text_input != NULL) {
		input_popup_set_focus(popup, text_input->input->focused_surface);
	} else {
		input_popup_set_focus(popup, NULL);
	}

	wl_list_insert(&relay->input_popups, &popup->link);
}

static void text_input_send_enter(struct sway_text_input *text_input,
		struct wlr_surface *surface) {
	wlr_text_input_v3_send_enter(text_input->input, surface);
	struct sway_input_popup *popup;
	wl_list_for_each(popup, &text_input->relay->input_popups, link) {
		input_popup_set_focus(popup, surface);
	}
}

static void text_input_send_leave(struct sway_text_input *text_input,
		struct wlr_surface *surface) {
	wlr_text_input_v3_send_leave(text_input->input);
	if (text_input->input->focused_surface == surface) {
		struct sway_input_popup *popup;
		wl_list_for_each(popup, &text_input->relay->input_popups, link) {
			input_popup_set_focus(popup, NULL);
		}
	}
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
	struct sway_input_popup *popup;
	wl_list_for_each(popup, &relay->input_popups, link) {
		input_popup_update(popup);
	}
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
		text_input_send_leave(text_input, text_input->input->focused_surface);
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
	struct sway_input_popup *popup;
	wl_list_for_each(popup, &relay->input_popups, link) {
		input_popup_update(popup);
	}
	wlr_input_method_v2_send_done(input_method);
	// TODO: pass intent
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
	wl_signal_add(&relay->input_method->events.new_popup_surface,
		&relay->input_method_new_popup_surface);
	relay->input_method_new_popup_surface.notify = handle_im_new_popup_surface;
	wl_signal_add(&relay->input_method->events.grab_keyboard,
		&relay->input_method_grab_keyboard);
	relay->input_method_grab_keyboard.notify = handle_im_grab_keyboard;
	wl_signal_add(&relay->input_method->events.destroy,
		&relay->input_method_destroy);
	relay->input_method_destroy.notify = handle_im_destroy;

	struct sway_text_input *text_input = relay_get_focusable_text_input(relay);
	if (text_input) {
		text_input_send_enter(text_input, text_input->pending_focused_surface);
		text_input_set_pending_focused_surface(text_input, NULL);
	}
}

void sway_input_method_relay_init(struct sway_seat *seat,
		struct sway_input_method_relay *relay) {
	relay->seat = seat;
	wl_list_init(&relay->text_inputs);
	wl_list_init(&relay->input_popups);

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
				text_input_send_leave(
					text_input, text_input->input->focused_surface);
				relay_disable_text_input(relay, text_input);
			} else {
				sway_log(SWAY_DEBUG, "IM relay set_focus already focused");
				continue;
			}
		}

		if (surface
				&& wl_resource_get_client(text_input->input->resource)
				== wl_resource_get_client(surface->resource)) {
			if (relay->input_method) {
				text_input_send_enter(text_input, surface);
			} else {
				text_input_set_pending_focused_surface(text_input, surface);
			}
		}
	}
}
