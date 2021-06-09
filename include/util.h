#ifndef _SWAY_UTIL_H
#define _SWAY_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <wayland-server-protocol.h>

enum movement_unit {
	MOVEMENT_UNIT_PX,
	MOVEMENT_UNIT_PPT,
	MOVEMENT_UNIT_DEFAULT,
	MOVEMENT_UNIT_INVALID,
};

struct movement_amount {
	int amount;
	enum movement_unit unit;
};

/*
 * Parse units such as "px" or "ppt"
 */
enum movement_unit parse_movement_unit(const char *unit);

/*
 * Parse arguments such as "10", "10px" or "10 px".
 * Returns the number of arguments consumed.
 */
int parse_movement_amount(int argc, char **argv,
		struct movement_amount *amount);

/**
 * Get the current time, in milliseconds.
 */

uint32_t get_current_time_msec(void);

/**
 * Wrap i into the range [0, max]
 */
int wrap(int i, int max);

/**
 * Given a string that represents an RGB(A) color, result will be set to a
 * uint32_t version of the color, as long as it is valid. If it is invalid,
 * then false will be returned and result will be untouched.
 */
bool parse_color(const char *color, uint32_t *result);

void color_to_rgba(float dest[static 4], uint32_t color);

/**
 * Given a string that represents a boolean, return the boolean value. This
 * function also takes in the current boolean value to support toggling. If
 * toggling is not desired, pass in true for current so that toggling values
 * get parsed as not true.
 */
bool parse_boolean(const char *boolean, bool current);

/**
 * Given a string that represents a floating point value, return a float.
 * Returns NAN on error.
 */
float parse_float(const char *value);

const char *sway_wl_output_subpixel_to_string(enum wl_output_subpixel subpixel);

bool sway_set_cloexec(int fd, bool cloexec);

void scroll_distance_add(float delta);
void scroll_distance_reset();
float scroll_distance_get();

#endif
