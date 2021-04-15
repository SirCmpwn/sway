#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "log.h"
#include "list.h"
#include "swaynag/render.h"
#include "swaynag/swaynag.h"
#include "swaynag/types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static void nop() {
	// Intentionally left blank
}

static bool terminal_execute(char *terminal, char *command) {
	char fname[] = "/tmp/swaynagXXXXXX";
	FILE *tmp= fdopen(mkstemp(fname), "w");
	if (!tmp) {
		sway_log(SWAY_ERROR, "Failed to create temp script");
		return false;
	}
	sway_log(SWAY_DEBUG, "Created temp script: %s", fname);
	fprintf(tmp, "#!/bin/sh\nrm %s\n%s", fname, command);
	fclose(tmp);
	chmod(fname, S_IRUSR | S_IWUSR | S_IXUSR);
	char *cmd = malloc(sizeof(char) * (strlen(terminal) + strlen(" -e ") + strlen(fname) + 1));
	sprintf(cmd, "%s -e %s", terminal, fname);
	execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
	sway_log_errno(SWAY_ERROR, "Failed to run command, execl() returned.");
	free(cmd);
	return false;
}

static void swaynag_button_execute(struct swaynag *swaynag,
		struct swaynag_button *button) {
	sway_log(SWAY_DEBUG, "Executing [%s]: %s", button->text, button->action);
	if (button->type == SWAYNAG_ACTION_DISMISS) {
		swaynag->run_display = false;
	} else if (button->type == SWAYNAG_ACTION_EXPAND) {
		swaynag->details.visible = !swaynag->details.visible;
		render_frame(swaynag);
	} else {
		pid_t pid = fork();
		if (pid < 0) {
			sway_log_errno(SWAY_DEBUG, "Failed to fork");
			return;
		} else if (pid == 0) {
			// Child process. Will be used to prevent zombie processes
			pid = fork();
			if (pid < 0) {
				sway_log_errno(SWAY_DEBUG, "Failed to fork");
				return;
			} else if (pid == 0) {
				// Child of the child. Will be reparented to the init process
				char *terminal = getenv("TERMINAL");
				if (button->terminal && terminal && strlen(terminal)) {
					sway_log(SWAY_DEBUG, "Found $TERMINAL: %s", terminal);
					if (!terminal_execute(terminal, button->action)) {
						swaynag_destroy(swaynag);
						_exit(EXIT_FAILURE);
					}
				} else {
					if (button->terminal) {
						sway_log(SWAY_DEBUG,
								"$TERMINAL not found. Running directly");
					}
					execl("/bin/sh", "/bin/sh", "-c", button->action, NULL);
					sway_log_errno(SWAY_DEBUG, "execl failed");
					_exit(EXIT_FAILURE);
				}
			}
			_exit(EXIT_SUCCESS);
		}

		if (button->dismiss) {
			swaynag->run_display = false;
		}

		if (waitpid(pid, NULL, 0) < 0) {
			sway_log_errno(SWAY_DEBUG, "waitpid failed");
		}
	}
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct swaynag *swaynag = data;
	swaynag->width = width;
	swaynag->height = height;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	render_frame(swaynag);
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *surface) {
	struct swaynag *swaynag = data;
	swaynag_destroy(swaynag);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void surface_enter(void *data, struct wl_surface *surface,
		struct wl_output *output) {
	struct swaynag *swaynag = data;
	struct swaynag_output *swaynag_output;
	wl_list_for_each(swaynag_output, &swaynag->outputs, link) {
		if (swaynag_output->wl_output == output) {
			sway_log(SWAY_DEBUG, "Surface enter on output %s",
					swaynag_output->name);
			swaynag->output = swaynag_output;
			swaynag->scale = swaynag->output->scale;
			render_frame(swaynag);
			break;
		}
	};
}

static const struct wl_surface_listener surface_listener = {
	.enter = surface_enter,
	.leave = nop,
};

static void update_cursor(struct swaynag_seat *seat) {
	struct swaynag_pointer *pointer = &seat->pointer;
	struct swaynag *swaynag = seat->swaynag;
	if (pointer->cursor_theme) {
		wl_cursor_theme_destroy(pointer->cursor_theme);
	}
	const char *cursor_theme = getenv("XCURSOR_THEME");
	unsigned cursor_size = 24;
	const char *env_cursor_size = getenv("XCURSOR_SIZE");
	if (env_cursor_size && strlen(env_cursor_size) > 0) {
		errno = 0;
		char *end;
		unsigned size = strtoul(env_cursor_size, &end, 10);
		if (!*end && errno == 0) {
			cursor_size = size;
		}
	}
	pointer->cursor_theme = wl_cursor_theme_load(
		cursor_theme, cursor_size * swaynag->scale, swaynag->shm);
	struct wl_cursor *cursor =
		wl_cursor_theme_get_cursor(pointer->cursor_theme, "left_ptr");
	pointer->cursor_image = cursor->images[0];
	wl_surface_set_buffer_scale(pointer->cursor_surface,
			swaynag->scale);
	wl_surface_attach(pointer->cursor_surface,
			wl_cursor_image_get_buffer(pointer->cursor_image), 0, 0);
	wl_pointer_set_cursor(pointer->pointer, pointer->serial,
			pointer->cursor_surface,
			pointer->cursor_image->hotspot_x / swaynag->scale,
			pointer->cursor_image->hotspot_y / swaynag->scale);
	wl_surface_damage_buffer(pointer->cursor_surface, 0, 0,
			INT32_MAX, INT32_MAX);
	wl_surface_commit(pointer->cursor_surface);
}

void update_all_cursors(struct swaynag *swaynag) {
	struct swaynag_seat *seat;
	wl_list_for_each(seat, &swaynag->seats, link) {
		if (seat->pointer.pointer) {
			update_cursor(seat);
		}
	}
}

static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct swaynag_seat *seat = data;
	struct swaynag_pointer *pointer = &seat->pointer;
	pointer->x = wl_fixed_to_int(surface_x);
	pointer->y = wl_fixed_to_int(surface_y);
	pointer->serial = serial;
	update_cursor(seat);
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct swaynag_seat *seat = data;
	seat->pointer.x = wl_fixed_to_int(surface_x);
	seat->pointer.y = wl_fixed_to_int(surface_y);
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct swaynag_seat *seat = data;
	struct swaynag *swaynag = seat->swaynag;

	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return;
	}

	double x = seat->pointer.x * swaynag->scale;
	double y = seat->pointer.y * swaynag->scale;
	for (int i = 0; i < swaynag->buttons->length; i++) {
		struct swaynag_button *nagbutton = swaynag->buttons->items[i];
		if (x >= nagbutton->x
				&& y >= nagbutton->y
				&& x < nagbutton->x + nagbutton->width
				&& y < nagbutton->y + nagbutton->height) {
			swaynag_button_execute(swaynag, nagbutton);
			return;
		}
	}

	if (swaynag->details.visible &&
			swaynag->details.total_lines != swaynag->details.visible_lines) {
		struct swaynag_button button_up = swaynag->details.button_up;
		if (x >= button_up.x
				&& y >= button_up.y
				&& x < button_up.x + button_up.width
				&& y < button_up.y + button_up.height
				&& swaynag->details.offset > 0) {
			swaynag->details.offset--;
			render_frame(swaynag);
			return;
		}

		struct swaynag_button button_down = swaynag->details.button_down;
		int bot = swaynag->details.total_lines;
		bot -= swaynag->details.visible_lines;
		if (x >= button_down.x
				&& y >= button_down.y
				&& x < button_down.x + button_down.width
				&& y < button_down.y + button_down.height
				&& swaynag->details.offset < bot) {
			swaynag->details.offset++;
			render_frame(swaynag);
			return;
		}
	}
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct swaynag_seat *seat = data;
	struct swaynag *swaynag = seat->swaynag;
	if (!swaynag->details.visible
			|| seat->pointer.x < swaynag->details.x
			|| seat->pointer.y < swaynag->details.y
			|| seat->pointer.x >= swaynag->details.x + swaynag->details.width
			|| seat->pointer.y >= swaynag->details.y + swaynag->details.height
			|| swaynag->details.total_lines == swaynag->details.visible_lines) {
		return;
	}

	int direction = wl_fixed_to_int(value);
	int bot = swaynag->details.total_lines - swaynag->details.visible_lines;
	if (direction < 0 && swaynag->details.offset > 0) {
		swaynag->details.offset--;
	} else if (direction > 0 && swaynag->details.offset < bot) {
		swaynag->details.offset++;
	}

	render_frame(swaynag);
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = wl_pointer_enter,
	.leave = nop,
	.motion = wl_pointer_motion,
	.button = wl_pointer_button,
	.axis = wl_pointer_axis,
	.frame = nop,
	.axis_source = nop,
	.axis_stop = nop,
	.axis_discrete = nop,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct swaynag_seat *seat = data;
	bool cap_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
	if (cap_pointer && !seat->pointer.pointer) {
		seat->pointer.pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->pointer.pointer,
				&pointer_listener, seat);
	} else if (!cap_pointer && seat->pointer.pointer) {
		wl_pointer_destroy(seat->pointer.pointer);
		seat->pointer.pointer = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = nop,
};

static void output_scale(void *data, struct wl_output *output,
		int32_t factor) {
	struct swaynag_output *swaynag_output = data;
	swaynag_output->scale = factor;
	if (swaynag_output->swaynag->output == swaynag_output) {
		swaynag_output->swaynag->scale = swaynag_output->scale;
		update_all_cursors(swaynag_output->swaynag);
		render_frame(swaynag_output->swaynag);
	}
}

static const struct wl_output_listener output_listener = {
	.geometry = nop,
	.mode = nop,
	.done = nop,
	.scale = output_scale,
};

static void xdg_output_handle_name(void *data,
		struct zxdg_output_v1 *xdg_output, const char *name) {
	struct swaynag_output *swaynag_output = data;
	char *outname = swaynag_output->swaynag->type->output;
	sway_log(SWAY_DEBUG, "Checking against output %s for %s", name, outname);
	if (!swaynag_output->swaynag->output && outname && name
			&& strcmp(outname, name) == 0) {
		sway_log(SWAY_DEBUG, "Using output %s", name);
		swaynag_output->swaynag->output = swaynag_output;
	}
	swaynag_output->name = strdup(name);
	zxdg_output_v1_destroy(xdg_output);
	swaynag_output->swaynag->querying_outputs--;
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = nop,
	.logical_size = nop,
	.done = nop,
	.name = xdg_output_handle_name,
	.description = nop,
};

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct swaynag *swaynag = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		swaynag->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct swaynag_seat *seat =
			calloc(1, sizeof(struct swaynag_seat));
		if (!seat) {
			return;
		}

		seat->swaynag = swaynag;
		seat->wl_name = name;
		seat->wl_seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);

		wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);

		wl_list_insert(&swaynag->seats, &seat->link);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		swaynag->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		if (!swaynag->output && swaynag->xdg_output_manager) {
			swaynag->querying_outputs++;
			struct swaynag_output *output =
				calloc(1, sizeof(struct swaynag_output));
			output->wl_output = wl_registry_bind(registry, name,
					&wl_output_interface, 3);
			output->wl_name = name;
			output->scale = 1;
			output->swaynag = swaynag;
			wl_list_insert(&swaynag->outputs, &output->link);
			wl_output_add_listener(output->wl_output,
					&output_listener, output);

			struct zxdg_output_v1 *xdg_output;
			xdg_output = zxdg_output_manager_v1_get_xdg_output(
					swaynag->xdg_output_manager, output->wl_output);
			zxdg_output_v1_add_listener(xdg_output,
					&xdg_output_listener, output);
		}
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		swaynag->layer_shell = wl_registry_bind(
				registry, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0
			&& version >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION) {
		swaynag->xdg_output_manager = wl_registry_bind(registry, name,
				&zxdg_output_manager_v1_interface,
				ZXDG_OUTPUT_V1_NAME_SINCE_VERSION);
	}
}

void swaynag_seat_destroy(struct swaynag_seat *seat) {
	if (seat->pointer.cursor_theme) {
		wl_cursor_theme_destroy(seat->pointer.cursor_theme);
	}
	if (seat->pointer.pointer) {
		wl_pointer_destroy(seat->pointer.pointer);
	}
	wl_seat_destroy(seat->wl_seat);
	wl_list_remove(&seat->link);
	free(seat);
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	struct swaynag *swaynag = data;
	if (swaynag->output->wl_name == name) {
		swaynag->run_display = false;
	}

	struct swaynag_seat *seat, *tmpseat;
	wl_list_for_each_safe(seat, tmpseat, &swaynag->seats, link) {
		if (seat->wl_name == name) {
			swaynag_seat_destroy(seat);
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

void swaynag_setup_cursors(struct swaynag *swaynag) {
	struct swaynag_seat *seat;

	wl_list_for_each(seat, &swaynag->seats, link) {
		struct swaynag_pointer *p = &seat->pointer;

		p->cursor_surface =
			wl_compositor_create_surface(swaynag->compositor);
		assert(p->cursor_surface);
	}
}

void swaynag_setup(struct swaynag *swaynag) {
	swaynag->display = wl_display_connect(NULL);
	if (!swaynag->display) {
		sway_abort("Unable to connect to the compositor. "
				"If your compositor is running, check or set the "
				"WAYLAND_DISPLAY environment variable.");
	}

	swaynag->scale = 1;

	struct wl_registry *registry = wl_display_get_registry(swaynag->display);
	wl_registry_add_listener(registry, &registry_listener, swaynag);
	if (wl_display_roundtrip(swaynag->display) < 0) {
		sway_abort("failed to register with the wayland display");
	}

	assert(swaynag->compositor && swaynag->layer_shell && swaynag->shm);

	while (swaynag->querying_outputs > 0) {
		if (wl_display_roundtrip(swaynag->display) < 0) {
			sway_log(SWAY_ERROR, "Error during outputs init.");
			swaynag_destroy(swaynag);
			exit(EXIT_FAILURE);
		}
	}

	if (!swaynag->output && swaynag->type->output) {
		sway_log(SWAY_ERROR, "Output '%s' not found", swaynag->type->output);
		swaynag_destroy(swaynag);
		exit(EXIT_FAILURE);
	}

	swaynag_setup_cursors(swaynag);

	swaynag->surface = wl_compositor_create_surface(swaynag->compositor);
	assert(swaynag->surface);
	wl_surface_add_listener(swaynag->surface, &surface_listener, swaynag);

	swaynag->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			swaynag->layer_shell, swaynag->surface,
			swaynag->output ? swaynag->output->wl_output : NULL,
			ZWLR_LAYER_SHELL_V1_LAYER_TOP, "swaynag");
	assert(swaynag->layer_surface);
	zwlr_layer_surface_v1_add_listener(swaynag->layer_surface,
			&layer_surface_listener, swaynag);
	zwlr_layer_surface_v1_set_anchor(swaynag->layer_surface,
			swaynag->type->anchors);

	wl_registry_destroy(registry);
}

void swaynag_run(struct swaynag *swaynag) {
	swaynag->run_display = true;
	render_frame(swaynag);
	while (swaynag->run_display
			&& wl_display_dispatch(swaynag->display) != -1) {
		// This is intentionally left blank
	}

	if (swaynag->display) {
		wl_display_disconnect(swaynag->display);
	}
}

void swaynag_destroy(struct swaynag *swaynag) {
	swaynag->run_display = false;

	free(swaynag->message);
	for (int i = 0; i < swaynag->buttons->length; ++i) {
		struct swaynag_button *button = swaynag->buttons->items[i];
		free(button->text);
		free(button->action);
		free(button);
	}
	list_free(swaynag->buttons);
	free(swaynag->details.message);
	free(swaynag->details.button_up.text);
	free(swaynag->details.button_down.text);

	if (swaynag->type) {
		swaynag_type_free(swaynag->type);
	}

	if (swaynag->layer_surface) {
		zwlr_layer_surface_v1_destroy(swaynag->layer_surface);
	}

	if (swaynag->surface) {
		wl_surface_destroy(swaynag->surface);
	}

	struct swaynag_seat *seat, *tmpseat;
	wl_list_for_each_safe(seat, tmpseat, &swaynag->seats, link) {
		swaynag_seat_destroy(seat);
	}

	if (&swaynag->buffers[0]) {
		destroy_buffer(&swaynag->buffers[0]);
	}

	if (&swaynag->buffers[1]) {
		destroy_buffer(&swaynag->buffers[1]);
	}

	if (swaynag->outputs.prev || swaynag->outputs.next) {
		struct swaynag_output *output, *temp;
		wl_list_for_each_safe(output, temp, &swaynag->outputs, link) {
			wl_output_destroy(output->wl_output);
			free(output->name);
			wl_list_remove(&output->link);
			free(output);
		};
	}

	if (swaynag->compositor) {
		wl_compositor_destroy(swaynag->compositor);
	}

	if (swaynag->shm) {
		wl_shm_destroy(swaynag->shm);
	}
}
