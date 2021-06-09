#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-protocol.h>
#include "log.h"
#include "util.h"

uint32_t get_current_time_msec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int wrap(int i, int max) {
	return ((i % max) + max) % max;
}

bool parse_color(const char *color, uint32_t *result) {
	if (color[0] == '#') {
		++color;
	}
	int len = strlen(color);
	if ((len != 6 && len != 8) || !isxdigit(color[0]) || !isxdigit(color[1])) {
		return false;
	}
	char *ptr;
	uint32_t parsed = strtoul(color, &ptr, 16);
	if (*ptr != '\0') {
		return false;
	}
	*result = len == 6 ? ((parsed << 8) | 0xFF) : parsed;
	return true;
}

void color_to_rgba(float dest[static 4], uint32_t color) {
	dest[0] = ((color >> 24) & 0xff) / 255.0;
	dest[1] = ((color >> 16) & 0xff) / 255.0;
	dest[2] = ((color >> 8) & 0xff) / 255.0;
	dest[3] = (color & 0xff) / 255.0;
}

bool parse_boolean(const char *boolean, bool current) {
	if (strcasecmp(boolean, "1") == 0
			|| strcasecmp(boolean, "yes") == 0
			|| strcasecmp(boolean, "on") == 0
			|| strcasecmp(boolean, "true") == 0
			|| strcasecmp(boolean, "enable") == 0
			|| strcasecmp(boolean, "enabled") == 0
			|| strcasecmp(boolean, "active") == 0) {
		return true;
	} else if (strcasecmp(boolean, "toggle") == 0) {
		return !current;
	}
	// All other values are false to match i3
	return false;
}

float parse_float(const char *value) {
	errno = 0;
	char *end;
	float flt = strtof(value, &end);
	if (*end || errno) {
		sway_log(SWAY_DEBUG, "Invalid float value '%s', defaulting to NAN", value);
		return NAN;
	}
	return flt;
}

enum movement_unit parse_movement_unit(const char *unit) {
	if (strcasecmp(unit, "px") == 0) {
		return MOVEMENT_UNIT_PX;
	}
	if (strcasecmp(unit, "ppt") == 0) {
		return MOVEMENT_UNIT_PPT;
	}
	if (strcasecmp(unit, "default") == 0) {
		return MOVEMENT_UNIT_DEFAULT;
	}
	return MOVEMENT_UNIT_INVALID;
}

int parse_movement_amount(int argc, char **argv,
		struct movement_amount *amount) {
	char *err;
	amount->amount = (int)strtol(argv[0], &err, 10);
	if (*err) {
		// e.g. 10px
		amount->unit = parse_movement_unit(err);
		return 1;
	}
	if (argc == 1) {
		amount->unit = MOVEMENT_UNIT_DEFAULT;
		return 1;
	}
	// Try the second argument
	amount->unit = parse_movement_unit(argv[1]);
	if (amount->unit == MOVEMENT_UNIT_INVALID) {
		amount->unit = MOVEMENT_UNIT_DEFAULT;
		return 1;
	}
	return 2;
}

const char *sway_wl_output_subpixel_to_string(enum wl_output_subpixel subpixel) {
	switch (subpixel) {
	case WL_OUTPUT_SUBPIXEL_UNKNOWN:
		return "unknown";
	case WL_OUTPUT_SUBPIXEL_NONE:
		return "none";
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
		return "rgb";
	case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
		return "bgr";
	case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
		return "vrgb";
	case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
		return "vbgr";
	}
	sway_assert(false, "Unknown value for wl_output_subpixel.");
	return NULL;
}

bool sway_set_cloexec(int fd, bool cloexec) {
	int flags = fcntl(fd, F_GETFD);
	if (flags == -1) {
		sway_log_errno(SWAY_ERROR, "fcntl failed");
		return false;
	}
	if (cloexec) {
		flags = flags | FD_CLOEXEC;
	} else {
		flags = flags & ~FD_CLOEXEC;
	}
	if (fcntl(fd, F_SETFD, flags) == -1) {
		sway_log_errno(SWAY_ERROR, "fcntl failed");
		return false;
	}
	return true;
}

float _distance_scrolled = 0;
void scroll_distance_add(float delta) {
	_distance_scrolled += delta;
}

void scroll_distance_reset() {
	_distance_scrolled = 0;
}

float scroll_distance_get() {
	return _distance_scrolled;
}
