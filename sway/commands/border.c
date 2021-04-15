#include "log.h"
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/input/cursor.h"
#include "sway/input/input-manager.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"

// A couple of things here:
// - view->border should never be B_CSD when the view is tiled, even when CSD is
//   in use (we set using_csd instead and render a sway border).
// - view->saved_border should be the last applied border when switching to CSD.
// - view->using_csd should always reflect whether CSD is applied or not.
static void set_border(struct sway_container *con,
		enum sway_container_border new_border) {
	if (con->view) {
		if (con->view->using_csd && new_border != B_CSD) {
			view_set_csd_from_server(con->view, false);
		} else if (!con->view->using_csd && new_border == B_CSD) {
			view_set_csd_from_server(con->view, true);
			con->saved_border = con->pending.border;
		}
	}
	if (new_border != B_CSD || container_is_floating(con)) {
		con->pending.border = new_border;
	}
	if (con->view) {
		con->view->using_csd = new_border == B_CSD;
	}
}

static void border_toggle(struct sway_container *con) {
	if (con->view && con->view->using_csd) {
		set_border(con, B_NONE);
		return;
	}
	switch (con->pending.border) {
	case B_NONE:
		set_border(con, B_PIXEL);
		break;
	case B_PIXEL:
		set_border(con, B_NORMAL);
		break;
	case B_NORMAL:
		if (con->view && con->view->xdg_decoration) {
			set_border(con, B_CSD);
		} else {
			set_border(con, B_NONE);
		}
		break;
	case B_CSD:
		// view->using_csd should be true so it would have returned above
		sway_assert(false, "Unreachable");
		break;
	}
}

struct cmd_results *cmd_border(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "border", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	struct sway_container *container = config->handler_context.container;
	if (!container || !container->view) {
		return cmd_results_new(CMD_INVALID, "Only views can have borders");
	}
	struct sway_view *view = container->view;

	if (strcmp(argv[0], "none") == 0) {
		set_border(container, B_NONE);
	} else if (strcmp(argv[0], "normal") == 0) {
		set_border(container, B_NORMAL);
	} else if (strcmp(argv[0], "pixel") == 0) {
		set_border(container, B_PIXEL);
	} else if (strcmp(argv[0], "csd") == 0) {
		if (!view->xdg_decoration) {
			return cmd_results_new(CMD_INVALID,
					"This window doesn't support client side decorations");
		}
		set_border(container, B_CSD);
	} else if (strcmp(argv[0], "toggle") == 0) {
		border_toggle(container);
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected 'border <none|normal|pixel|csd|toggle>' "
				"or 'border pixel <px>'");
	}
	if (argc == 2) {
		container->pending.border_thickness = atoi(argv[1]);
	}

	if (container_is_floating(container)) {
		container_set_geometry_from_content(container);
	}

	arrange_container(container);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
