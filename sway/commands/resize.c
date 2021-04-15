#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/util/edges.h>
#include "sway/commands.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "log.h"
#include "util.h"

#define AXIS_HORIZONTAL (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
#define AXIS_VERTICAL   (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)

static uint32_t parse_resize_axis(const char *axis) {
	if (strcasecmp(axis, "width") == 0 || strcasecmp(axis, "horizontal") == 0) {
		return AXIS_HORIZONTAL;
	}
	if (strcasecmp(axis, "height") == 0 || strcasecmp(axis, "vertical") == 0) {
		return AXIS_VERTICAL;
	}
	if (strcasecmp(axis, "up") == 0) {
		return WLR_EDGE_TOP;
	}
	if (strcasecmp(axis, "down") == 0) {
		return WLR_EDGE_BOTTOM;
	}
	if (strcasecmp(axis, "left") == 0) {
		return WLR_EDGE_LEFT;
	}
	if (strcasecmp(axis, "right") == 0) {
		return WLR_EDGE_RIGHT;
	}
	return WLR_EDGE_NONE;
}

static bool is_horizontal(uint32_t axis) {
	return axis & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
}

struct sway_container *container_find_resize_parent(struct sway_container *con,
		uint32_t axis) {
	enum sway_container_layout parallel_layout =
		is_horizontal(axis) ? L_HORIZ : L_VERT;
	bool allow_first = axis != WLR_EDGE_TOP && axis != WLR_EDGE_LEFT;
	bool allow_last = axis != WLR_EDGE_RIGHT && axis != WLR_EDGE_BOTTOM;

	while (con) {
		list_t *siblings = container_get_siblings(con);
		int index = container_sibling_index(con);
		if (container_parent_layout(con) == parallel_layout &&
				siblings->length > 1 && (allow_first || index > 0) &&
				(allow_last || index < siblings->length - 1)) {
			return con;
		}
		con = con->pending.parent;
	}

	return NULL;
}

void container_resize_tiled(struct sway_container *con,
		uint32_t axis, int amount) {
	if (!con) {
		return;
	}

	con = container_find_resize_parent(con, axis);
	if (!con) {
		// Can't resize in this direction
		return;
	}

	// For HORIZONTAL or VERTICAL, we are growing in two directions so select
	// both adjacent siblings. For RIGHT or DOWN, just select the next sibling.
	// For LEFT or UP, convert it to a RIGHT or DOWN resize and reassign con to
	// the previous sibling.
	struct sway_container *prev = NULL;
	struct sway_container *next = NULL;
	list_t *siblings = container_get_siblings(con);
	int index = container_sibling_index(con);

	if (axis == AXIS_HORIZONTAL || axis == AXIS_VERTICAL) {
		if (index == 0) {
			next = siblings->items[1];
		} else if (index == siblings->length - 1) {
			// Convert edge to top/left
			next = con;
			con = siblings->items[index - 1];
			amount = -amount;
		} else {
			prev = siblings->items[index - 1];
			next = siblings->items[index + 1];
		}
	} else if (axis == WLR_EDGE_TOP || axis == WLR_EDGE_LEFT) {
		if (!sway_assert(index > 0, "Didn't expect first child")) {
			return;
		}
		next = con;
		con = siblings->items[index - 1];
		amount = -amount;
	} else {
		if (!sway_assert(index < siblings->length - 1,
					"Didn't expect last child")) {
			return;
		}
		next = siblings->items[index + 1];
	}

	// Apply new dimensions
	int sibling_amount = prev ? ceil((double)amount / 2.0) : amount;

	if (is_horizontal(axis)) {
		if (con->pending.width + amount < MIN_SANE_W) {
			return;
		}
		if (next->pending.width - sibling_amount < MIN_SANE_W) {
			return;
		}
		if (prev && prev->pending.width - sibling_amount < MIN_SANE_W) {
			return;
		}
		if (con->child_total_width <= 0) {
			return;
		}

		// We're going to resize so snap all the width fractions to full pixels
		// to avoid rounding issues
		list_t *siblings = container_get_siblings(con);
		for (int i = 0; i < siblings->length; ++i) {
			struct sway_container *con = siblings->items[i];
			con->width_fraction = con->pending.width / con->child_total_width;
		}

		double amount_fraction = (double)amount / con->child_total_width;
		double sibling_amount_fraction =
			prev ? amount_fraction / 2.0 : amount_fraction;

		con->width_fraction += amount_fraction;
		next->width_fraction -= sibling_amount_fraction;
		if (prev) {
			prev->width_fraction -= sibling_amount_fraction;
		}
	} else {
		if (con->pending.height + amount < MIN_SANE_H) {
			return;
		}
		if (next->pending.height - sibling_amount < MIN_SANE_H) {
			return;
		}
		if (prev && prev->pending.height - sibling_amount < MIN_SANE_H) {
			return;
		}
		if (con->child_total_height <= 0) {
			return;
		}

		// We're going to resize so snap all the height fractions to full pixels
		// to avoid rounding issues
		list_t *siblings = container_get_siblings(con);
		for (int i = 0; i < siblings->length; ++i) {
			struct sway_container *con = siblings->items[i];
			con->height_fraction = con->pending.height / con->child_total_height;
		}

		double amount_fraction = (double)amount / con->child_total_height;
		double sibling_amount_fraction =
			prev ? amount_fraction / 2.0 : amount_fraction;

		con->height_fraction += amount_fraction;
		next->height_fraction -= sibling_amount_fraction;
		if (prev) {
			prev->height_fraction -= sibling_amount_fraction;
		}
	}

	if (con->pending.parent) {
		arrange_container(con->pending.parent);
	} else {
		arrange_workspace(con->pending.workspace);
	}
}

/**
 * Implement `resize <grow|shrink>` for a floating container.
 */
static struct cmd_results *resize_adjust_floating(uint32_t axis,
		struct movement_amount *amount) {
	struct sway_container *con = config->handler_context.container;
	int grow_width = 0, grow_height = 0;

	if (is_horizontal(axis)) {
		grow_width = amount->amount;
	} else {
		grow_height = amount->amount;
	}

	// Make sure we're not adjusting beyond floating min/max size
	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	if (con->pending.width + grow_width < min_width) {
		grow_width = min_width - con->pending.width;
	} else if (con->pending.width + grow_width > max_width) {
		grow_width = max_width - con->pending.width;
	}
	if (con->pending.height + grow_height < min_height) {
		grow_height = min_height - con->pending.height;
	} else if (con->pending.height + grow_height > max_height) {
		grow_height = max_height - con->pending.height;
	}
	int grow_x = 0, grow_y = 0;

	if (axis == AXIS_HORIZONTAL) {
		grow_x = -grow_width / 2;
	} else if (axis == AXIS_VERTICAL) {
		grow_y = -grow_height / 2;
	} else if (axis == WLR_EDGE_TOP) {
		grow_y = -grow_height;
	} else if (axis == WLR_EDGE_LEFT) {
		grow_x = -grow_width;
	}
	if (grow_width == 0 && grow_height == 0) {
		return cmd_results_new(CMD_INVALID, "Cannot resize any further");
	}
	con->pending.x += grow_x;
	con->pending.y += grow_y;
	con->pending.width += grow_width;
	con->pending.height += grow_height;

	con->pending.content_x += grow_x;
	con->pending.content_y += grow_y;
	con->pending.content_width += grow_width;
	con->pending.content_height += grow_height;

	arrange_container(con);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize <grow|shrink>` for a tiled container.
 */
static struct cmd_results *resize_adjust_tiled(uint32_t axis,
		struct movement_amount *amount) {
	struct sway_container *current = config->handler_context.container;

	if (amount->unit == MOVEMENT_UNIT_DEFAULT) {
		amount->unit = MOVEMENT_UNIT_PPT;
	}
	if (amount->unit == MOVEMENT_UNIT_PPT) {
		float pct = amount->amount / 100.0f;

		if (is_horizontal(axis)) {
			amount->amount = (float)current->pending.width * pct;
		} else {
			amount->amount = (float)current->pending.height * pct;
		}
	}

	double old_width = current->width_fraction;
	double old_height = current->height_fraction;
	container_resize_tiled(current, axis, amount->amount);
	if (current->width_fraction == old_width &&
			current->height_fraction == old_height) {
		return cmd_results_new(CMD_INVALID, "Cannot resize any further");
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize set` for a tiled container.
 */
static struct cmd_results *resize_set_tiled(struct sway_container *con,
		struct movement_amount *width, struct movement_amount *height) {
	if (width->amount) {
		if (width->unit == MOVEMENT_UNIT_PPT ||
				width->unit == MOVEMENT_UNIT_DEFAULT) {
			// Convert to px
			struct sway_container *parent = con->pending.parent;
			while (parent && parent->pending.layout != L_HORIZ) {
				parent = parent->pending.parent;
			}
			if (parent) {
				width->amount = parent->pending.width * width->amount / 100;
			} else {
				width->amount = con->pending.workspace->width * width->amount / 100;
			}
			width->unit = MOVEMENT_UNIT_PX;
		}
		if (width->unit == MOVEMENT_UNIT_PX) {
			container_resize_tiled(con, AXIS_HORIZONTAL,
					width->amount - con->pending.width);
		}
	}

	if (height->amount) {
		if (height->unit == MOVEMENT_UNIT_PPT ||
				height->unit == MOVEMENT_UNIT_DEFAULT) {
			// Convert to px
			struct sway_container *parent = con->pending.parent;
			while (parent && parent->pending.layout != L_VERT) {
				parent = parent->pending.parent;
			}
			if (parent) {
				height->amount = parent->pending.height * height->amount / 100;
			} else {
				height->amount = con->pending.workspace->height * height->amount / 100;
			}
			height->unit = MOVEMENT_UNIT_PX;
		}
		if (height->unit == MOVEMENT_UNIT_PX) {
			container_resize_tiled(con, AXIS_VERTICAL,
					height->amount - con->pending.height);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize set` for a floating container.
 */
static struct cmd_results *resize_set_floating(struct sway_container *con,
		struct movement_amount *width, struct movement_amount *height) {
	int min_width, max_width, min_height, max_height, grow_width = 0, grow_height = 0;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);

	if (width->amount) {
		switch (width->unit) {
		case MOVEMENT_UNIT_PPT:
			if (container_is_scratchpad_hidden(con)) {
				return cmd_results_new(CMD_FAILURE,
						"Cannot resize a hidden scratchpad container by ppt");
			}
			// Convert to px
			width->amount = con->pending.workspace->width * width->amount / 100;
			width->unit = MOVEMENT_UNIT_PX;
			// Falls through
		case MOVEMENT_UNIT_PX:
		case MOVEMENT_UNIT_DEFAULT:
			width->amount = fmax(min_width, fmin(width->amount, max_width));
			grow_width = width->amount - con->pending.width;
			con->pending.x -= grow_width / 2;
			con->pending.width = width->amount;
			break;
		case MOVEMENT_UNIT_INVALID:
			sway_assert(false, "invalid width unit");
			break;
		}
	}

	if (height->amount) {
		switch (height->unit) {
		case MOVEMENT_UNIT_PPT:
			if (container_is_scratchpad_hidden(con)) {
				return cmd_results_new(CMD_FAILURE,
						"Cannot resize a hidden scratchpad container by ppt");
			}
			// Convert to px
			height->amount = con->pending.workspace->height * height->amount / 100;
			height->unit = MOVEMENT_UNIT_PX;
			// Falls through
		case MOVEMENT_UNIT_PX:
		case MOVEMENT_UNIT_DEFAULT:
			height->amount = fmax(min_height, fmin(height->amount, max_height));
			grow_height = height->amount - con->pending.height;
			con->pending.y -= grow_height / 2;
			con->pending.height = height->amount;
			break;
		case MOVEMENT_UNIT_INVALID:
			sway_assert(false, "invalid height unit");
			break;
		}
	}

	con->pending.content_x -= grow_width / 2;
	con->pending.content_y -= grow_height / 2;
	con->pending.content_width += grow_width;
	con->pending.content_height += grow_height;

	arrange_container(con);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * resize set <args>
 *
 * args: [width] <width> [px|ppt]
 *     : height <height> [px|ppt]
 *     : [width] <width> [px|ppt] [height] <height> [px|ppt]
 */
static struct cmd_results *cmd_resize_set(int argc, char **argv) {
	struct cmd_results *error;
	if ((error = checkarg(argc, "resize", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	const char usage[] = "Expected 'resize set [width] <width> [px|ppt]' or "
		"'resize set height <height> [px|ppt]' or "
		"'resize set [width] <width> [px|ppt] [height] <height> [px|ppt]'";

	// Width
	struct movement_amount width = {0};
	if (argc >= 2 && !strcmp(argv[0], "width") && strcmp(argv[1], "height")) {
		argc--; argv++;
	}
	if (strcmp(argv[0], "height")) {
		int num_consumed_args = parse_movement_amount(argc, argv, &width);
		argc -= num_consumed_args;
		argv += num_consumed_args;
		if (width.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, usage);
		}
	}

	// Height
	struct movement_amount height = {0};
	if (argc) {
		if (argc >= 2 && !strcmp(argv[0], "height")) {
			argc--; argv++;
		}
		int num_consumed_args = parse_movement_amount(argc, argv, &height);
		if (argc > num_consumed_args) {
			return cmd_results_new(CMD_INVALID, usage);
		}
		if (width.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, usage);
		}
	}

	// If 0, don't resize that dimension
	struct sway_container *con = config->handler_context.container;
	if (width.amount <= 0) {
		width.amount = con->pending.width;
	}
	if (height.amount <= 0) {
		height.amount = con->pending.height;
	}

	if (container_is_floating(con)) {
		return resize_set_floating(con, &width, &height);
	}
	return resize_set_tiled(con, &width, &height);
}

/**
 * resize <grow|shrink> <args>
 *
 * args: <direction>
 * args: <direction> <amount> <unit>
 * args: <direction> <amount> <unit> or <amount> <other_unit>
 */
static struct cmd_results *cmd_resize_adjust(int argc, char **argv,
		int multiplier) {
	const char usage[] = "Expected 'resize grow|shrink <direction> "
		"[<amount> px|ppt [or <amount> px|ppt]]'";
	uint32_t axis = parse_resize_axis(*argv);
	if (axis == WLR_EDGE_NONE) {
		return cmd_results_new(CMD_INVALID, usage);
	}
	--argc; ++argv;

	// First amount
	struct movement_amount first_amount;
	if (argc) {
		int num_consumed_args = parse_movement_amount(argc, argv, &first_amount);
		argc -= num_consumed_args;
		argv += num_consumed_args;
		if (first_amount.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, usage);
		}
	} else {
		first_amount.amount = 10;
		first_amount.unit = MOVEMENT_UNIT_DEFAULT;
	}

	// "or"
	if (argc) {
		if (strcmp(*argv, "or") != 0) {
			return cmd_results_new(CMD_INVALID, usage);
		}
		--argc; ++argv;
	}

	// Second amount
	struct movement_amount second_amount;
	if (argc) {
		int num_consumed_args = parse_movement_amount(argc, argv, &second_amount);
		if (argc > num_consumed_args) {
			return cmd_results_new(CMD_INVALID, usage);
		}
		if (second_amount.unit == MOVEMENT_UNIT_INVALID) {
			return cmd_results_new(CMD_INVALID, usage);
		}
	} else {
		second_amount.amount = 0;
		second_amount.unit = MOVEMENT_UNIT_INVALID;
	}

	first_amount.amount *= multiplier;
	second_amount.amount *= multiplier;

	struct sway_container *con = config->handler_context.container;
	if (container_is_floating(con)) {
		// Floating containers can only resize in px. Choose an amount which
		// uses px, with fallback to an amount that specified no unit.
		if (first_amount.unit == MOVEMENT_UNIT_PX) {
			return resize_adjust_floating(axis, &first_amount);
		} else if (second_amount.unit == MOVEMENT_UNIT_PX) {
			return resize_adjust_floating(axis, &second_amount);
		} else if (first_amount.unit == MOVEMENT_UNIT_DEFAULT) {
			return resize_adjust_floating(axis, &first_amount);
		} else if (second_amount.unit == MOVEMENT_UNIT_DEFAULT) {
			return resize_adjust_floating(axis, &second_amount);
		} else {
			return cmd_results_new(CMD_INVALID,
					"Floating containers cannot use ppt measurements");
		}
	}

	// For tiling, prefer ppt -> default -> px
	if (first_amount.unit == MOVEMENT_UNIT_PPT) {
		return resize_adjust_tiled(axis, &first_amount);
	} else if (second_amount.unit == MOVEMENT_UNIT_PPT) {
		return resize_adjust_tiled(axis, &second_amount);
	} else if (first_amount.unit == MOVEMENT_UNIT_DEFAULT) {
		return resize_adjust_tiled(axis, &first_amount);
	} else if (second_amount.unit == MOVEMENT_UNIT_DEFAULT) {
		return resize_adjust_tiled(axis, &second_amount);
	} else {
		return resize_adjust_tiled(axis, &first_amount);
	}
}

struct cmd_results *cmd_resize(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct sway_container *current = config->handler_context.container;
	if (!current) {
		return cmd_results_new(CMD_INVALID, "Cannot resize nothing");
	}

	struct cmd_results *error;
	if ((error = checkarg(argc, "resize", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	if (strcasecmp(argv[0], "set") == 0) {
		return cmd_resize_set(argc - 1, &argv[1]);
	}
	if (strcasecmp(argv[0], "grow") == 0) {
		return cmd_resize_adjust(argc - 1, &argv[1], 1);
	}
	if (strcasecmp(argv[0], "shrink") == 0) {
		return cmd_resize_adjust(argc - 1, &argv[1], -1);
	}

	const char usage[] = "Expected 'resize <shrink|grow> "
		"<width|height|up|down|left|right> [<amount>] [px|ppt]'";

	return cmd_results_new(CMD_INVALID, usage);
}
