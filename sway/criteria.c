#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <strings.h>
#include <pcre.h>
#include "sway/criteria.h"
#include "sway/tree/container.h"
#include "sway/config.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "stringop.h"
#include "list.h"
#include "log.h"
#include "config.h"

bool criteria_is_empty(struct criteria *criteria) {
	return !criteria->title
		&& !criteria->shell
		&& !criteria->app_id
		&& !criteria->con_mark
		&& !criteria->con_id
#if HAVE_XWAYLAND
		&& !criteria->class
		&& !criteria->id
		&& !criteria->instance
		&& !criteria->window_role
		&& criteria->window_type == ATOM_LAST
#endif
		&& !criteria->floating
		&& !criteria->tiling
		&& !criteria->urgent
		&& !criteria->workspace
		&& !criteria->pid;
}

// The error pointer is used for parsing functions, and saves having to pass it
// as an argument in several places.
char *error = NULL;

// Returns error string on failure or NULL otherwise.
static bool generate_regex(pcre **regex, char *value) {
	const char *reg_err;
	int offset;

	*regex = pcre_compile(value, PCRE_UTF8 | PCRE_UCP, &reg_err, &offset, NULL);

	if (!*regex) {
		const char *fmt = "Regex compilation for '%s' failed: %s";
		int len = strlen(fmt) + strlen(value) + strlen(reg_err) - 3;
		error = malloc(len);
		snprintf(error, len, fmt, value, reg_err);
		return false;
	}

	return true;
}

static bool pattern_create(struct pattern **pattern, char *value) {
	*pattern = calloc(1, sizeof(struct pattern));
	if (!*pattern) {
		sway_log(SWAY_ERROR, "Failed to allocate pattern");
	}

	if (strcmp(value, "__focused__") == 0) {
		(*pattern)->match_type = PATTERN_FOCUSED;
	} else {
		(*pattern)->match_type = PATTERN_PCRE;
		if (!generate_regex(&(*pattern)->regex, value)) {
			return false;
		};
	}
	return true;
}

static void pattern_destroy(struct pattern *pattern) {
	if (pattern) {
		if (pattern->regex) {
			pcre_free(pattern->regex);
		}
		free(pattern);
	}
}

void criteria_destroy(struct criteria *criteria) {
	pattern_destroy(criteria->title);
	pattern_destroy(criteria->shell);
	pattern_destroy(criteria->app_id);
#if HAVE_XWAYLAND
	pattern_destroy(criteria->class);
	pattern_destroy(criteria->instance);
	pattern_destroy(criteria->window_role);
#endif
	pattern_destroy(criteria->con_mark);
	free(criteria->workspace);
	free(criteria->cmdlist);
	free(criteria->raw);
	free(criteria);
}

static int regex_cmp(const char *item, const pcre *regex) {
	return pcre_exec(regex, NULL, item, strlen(item), 0, 0, NULL, 0);
}

#if HAVE_XWAYLAND
static bool view_has_window_type(struct sway_view *view, enum atom_name name) {
	if (view->type != SWAY_VIEW_XWAYLAND) {
		return false;
	}
	struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
	struct sway_xwayland *xwayland = &server.xwayland;
	xcb_atom_t desired_atom = xwayland->atoms[name];
	for (size_t i = 0; i < surface->window_type_len; ++i) {
		if (surface->window_type[i] == desired_atom) {
			return true;
		}
	}
	return false;
}
#endif

static int cmp_urgent(const void *_a, const void *_b) {
	struct sway_view *a = *(void **)_a;
	struct sway_view *b = *(void **)_b;

	if (a->urgent.tv_sec < b->urgent.tv_sec) {
		return -1;
	} else if (a->urgent.tv_sec > b->urgent.tv_sec) {
		return 1;
	}
	if (a->urgent.tv_nsec < b->urgent.tv_nsec) {
		return -1;
	} else if (a->urgent.tv_nsec > b->urgent.tv_nsec) {
		return 1;
	}
	return 0;
}

static void find_urgent_iterator(struct sway_container *con, void *data) {
	if (!con->view || !view_is_urgent(con->view)) {
		return;
	}
	list_t *urgent_views = data;
	list_add(urgent_views, con->view);
}

static bool has_container_criteria(struct criteria *criteria) {
	return criteria->con_mark || criteria->con_id;
}

static bool criteria_matches_container(struct criteria *criteria,
		struct sway_container *container) {
	if (criteria->con_mark) {
		bool exists = false;
		struct sway_container *con = container;
		for (int i = 0; i < con->marks->length; ++i) {
			if (regex_cmp(con->marks->items[i], criteria->con_mark->regex) == 0) {
				exists = true;
				break;
			}
		}
		if (!exists) {
			return false;
		}
	}

	if (criteria->con_id) { // Internal ID
		if (container->node.id != criteria->con_id) {
			return false;
		}
	}

	return true;
}

static bool criteria_matches_view(struct criteria *criteria,
		struct sway_view *view) {
	struct sway_seat *seat = input_manager_current_seat();
	struct sway_container *focus = seat_get_focused_container(seat);
	struct sway_view *focused = focus ? focus->view : NULL;

	if (criteria->title) {
		const char *title = view_get_title(view);
		if (!title) {
			return false;
		}

		switch (criteria->title->match_type) {
		case PATTERN_FOCUSED:
			if (focused && lenient_strcmp(title, view_get_title(focused))) {
				return false;
			}
			break;
		case PATTERN_PCRE:
			if (regex_cmp(title, criteria->title->regex) != 0) {
				return false;
			}
			break;
		}
	}

	if (criteria->shell) {
		const char *shell = view_get_shell(view);
		if (!shell) {
			return false;
		}

		switch (criteria->shell->match_type) {
		case PATTERN_FOCUSED:
			if (focused && strcmp(shell, view_get_shell(focused))) {
				return false;
			}
			break;
		case PATTERN_PCRE:
			if (regex_cmp(shell, criteria->shell->regex) != 0) {
				return false;
			}
			break;
		}
	}

	if (criteria->app_id) {
		const char *app_id = view_get_app_id(view);
		if (!app_id) {
			return false;
		}

		switch (criteria->app_id->match_type) {
		case PATTERN_FOCUSED:
			if (focused && lenient_strcmp(app_id, view_get_app_id(focused))) {
				return false;
			}
			break;
		case PATTERN_PCRE:
			if (regex_cmp(app_id, criteria->app_id->regex) != 0) {
				return false;
			}
			break;
		}
	}

	if (!criteria_matches_container(criteria, view->container)) {
		return false;
	}

#if HAVE_XWAYLAND
	if (criteria->id) { // X11 window ID
		uint32_t x11_window_id = view_get_x11_window_id(view);
		if (!x11_window_id || x11_window_id != criteria->id) {
			return false;
		}
	}

	if (criteria->class) {
		const char *class = view_get_class(view);
		if (!class) {
			return false;
		}

		switch (criteria->class->match_type) {
		case PATTERN_FOCUSED:
			if (focused && lenient_strcmp(class, view_get_class(focused))) {
				return false;
			}
			break;
		case PATTERN_PCRE:
			if (regex_cmp(class, criteria->class->regex) != 0) {
				return false;
			}
			break;
		}
	}

	if (criteria->instance) {
		const char *instance = view_get_instance(view);
		if (!instance) {
			return false;
		}

		switch (criteria->instance->match_type) {
		case PATTERN_FOCUSED:
			if (focused && strcmp(instance, view_get_instance(focused))) {
				return false;
			}
			break;
		case PATTERN_PCRE:
			if (regex_cmp(instance, criteria->instance->regex) != 0) {
				return false;
			}
			break;
		}
	}

	if (criteria->window_role) {
		const char *window_role = view_get_window_role(view);
		if (!window_role) {
			return false;
		}

		switch (criteria->window_role->match_type) {
		case PATTERN_FOCUSED:
			if (focused && strcmp(window_role, view_get_window_role(focused))) {
				return false;
			}
			break;
		case PATTERN_PCRE:
			if (regex_cmp(window_role, criteria->window_role->regex) != 0) {
				return false;
			}
			break;
		}
	}

	if (criteria->window_type != ATOM_LAST) {
		if (!view_has_window_type(view, criteria->window_type)) {
			return false;
		}
	}
#endif

	if (criteria->floating) {
		if (!container_is_floating(view->container)) {
			return false;
		}
	}

	if (criteria->tiling) {
		if (container_is_floating(view->container)) {
			return false;
		}
	}

	if (criteria->urgent) {
		if (!view_is_urgent(view)) {
			return false;
		}
		list_t *urgent_views = create_list();
		root_for_each_container(find_urgent_iterator, urgent_views);
		list_stable_sort(urgent_views, cmp_urgent);
		struct sway_view *target;
		if (criteria->urgent == 'o') { // oldest
			target = urgent_views->items[0];
		} else { // latest
			target = urgent_views->items[urgent_views->length - 1];
		}
		list_free(urgent_views);
		if (view != target) {
			return false;
		}
	}

	if (criteria->workspace) {
		struct sway_workspace *ws = view->container->pending.workspace;
		if (!ws) {
			return false;
		}

		switch (criteria->workspace->match_type) {
		case PATTERN_FOCUSED:
			if (focused &&
					strcmp(ws->name, focused->container->pending.workspace->name)) {
				return false;
			}
			break;
		case PATTERN_PCRE:
			if (regex_cmp(ws->name, criteria->workspace->regex) != 0) {
				return false;
			}
			break;
		}
	}

	if (criteria->pid) {
		if (criteria->pid != view->pid) {
			return false;
		}
	}

	return true;
}

list_t *criteria_for_view(struct sway_view *view, enum criteria_type types) {
	list_t *criterias = config->criteria;
	list_t *matches = create_list();
	for (int i = 0; i < criterias->length; ++i) {
		struct criteria *criteria = criterias->items[i];
		if ((criteria->type & types) && criteria_matches_view(criteria, view)) {
			list_add(matches, criteria);
		}
	}
	return matches;
}

struct match_data {
	struct criteria *criteria;
	list_t *matches;
};

static void criteria_get_containers_iterator(struct sway_container *container,
		void *data) {
	struct match_data *match_data = data;
	if (container->view) {
		if (criteria_matches_view(match_data->criteria, container->view)) {
			list_add(match_data->matches, container);
		}
	} else if (has_container_criteria(match_data->criteria)) {
		if (criteria_matches_container(match_data->criteria, container)) {
			list_add(match_data->matches, container);
		}
	}
}

list_t *criteria_get_containers(struct criteria *criteria) {
	list_t *matches = create_list();
	struct match_data data = {
		.criteria = criteria,
		.matches = matches,
	};
	root_for_each_container(criteria_get_containers_iterator, &data);
	return matches;
}

#if HAVE_XWAYLAND
static enum atom_name parse_window_type(const char *type) {
	if (strcasecmp(type, "normal") == 0) {
		return NET_WM_WINDOW_TYPE_NORMAL;
	} else if (strcasecmp(type, "dialog") == 0) {
		return NET_WM_WINDOW_TYPE_DIALOG;
	} else if (strcasecmp(type, "utility") == 0) {
		return NET_WM_WINDOW_TYPE_UTILITY;
	} else if (strcasecmp(type, "toolbar") == 0) {
		return NET_WM_WINDOW_TYPE_TOOLBAR;
	} else if (strcasecmp(type, "splash") == 0) {
		return NET_WM_WINDOW_TYPE_SPLASH;
	} else if (strcasecmp(type, "menu") == 0) {
		return NET_WM_WINDOW_TYPE_MENU;
	} else if (strcasecmp(type, "dropdown_menu") == 0) {
		return NET_WM_WINDOW_TYPE_DROPDOWN_MENU;
	} else if (strcasecmp(type, "popup_menu") == 0) {
		return NET_WM_WINDOW_TYPE_POPUP_MENU;
	} else if (strcasecmp(type, "tooltip") == 0) {
		return NET_WM_WINDOW_TYPE_TOOLTIP;
	} else if (strcasecmp(type, "notification") == 0) {
		return NET_WM_WINDOW_TYPE_NOTIFICATION;
	}
	return ATOM_LAST; // ie. invalid
}
#endif

enum criteria_token {
	T_APP_ID,
	T_CON_ID,
	T_CON_MARK,
	T_FLOATING,
#if HAVE_XWAYLAND
	T_CLASS,
	T_ID,
	T_INSTANCE,
	T_WINDOW_ROLE,
	T_WINDOW_TYPE,
#endif
	T_SHELL,
	T_TILING,
	T_TITLE,
	T_URGENT,
	T_WORKSPACE,
	T_PID,

	T_INVALID,
};

static enum criteria_token token_from_name(char *name) {
	if (strcmp(name, "app_id") == 0) {
		return T_APP_ID;
	} else if (strcmp(name, "con_id") == 0) {
		return T_CON_ID;
	} else if (strcmp(name, "con_mark") == 0) {
		return T_CON_MARK;
#if HAVE_XWAYLAND
	} else if (strcmp(name, "class") == 0) {
		return T_CLASS;
	} else if (strcmp(name, "id") == 0) {
		return T_ID;
	} else if (strcmp(name, "instance") == 0) {
		return T_INSTANCE;
	} else if (strcmp(name, "window_role") == 0) {
		return T_WINDOW_ROLE;
	} else if (strcmp(name, "window_type") == 0) {
		return T_WINDOW_TYPE;
#endif
	} else if (strcmp(name, "shell") == 0) {
		return T_SHELL;
	} else if (strcmp(name, "title") == 0) {
		return T_TITLE;
	} else if (strcmp(name, "urgent") == 0) {
		return T_URGENT;
	} else if (strcmp(name, "workspace") == 0) {
		return T_WORKSPACE;
	} else if (strcmp(name, "tiling") == 0) {
		return T_TILING;
	} else if (strcmp(name, "floating") == 0) {
		return T_FLOATING;
	} else if (strcmp(name, "pid") == 0) {
		return T_PID;
	}
	return T_INVALID;
}

static bool parse_token(struct criteria *criteria, char *name, char *value) {
	enum criteria_token token = token_from_name(name);
	if (token == T_INVALID) {
		const char *fmt = "Token '%s' is not recognized";
		int len = strlen(fmt) + strlen(name) - 1;
		error = malloc(len);
		snprintf(error, len, fmt, name);
		return false;
	}

	// Require value, unless token is floating or tiled
	if (!value && token != T_FLOATING && token != T_TILING) {
		const char *fmt = "Token '%s' requires a value";
		int len = strlen(fmt) + strlen(name) - 1;
		error = malloc(len);
		snprintf(error, len, fmt, name);
		return false;
	}

	char *endptr = NULL;
	switch (token) {
	case T_TITLE:
		pattern_create(&criteria->title, value);
		break;
	case T_SHELL:
		pattern_create(&criteria->shell, value);
		break;
	case T_APP_ID:
		pattern_create(&criteria->app_id, value);
		break;
	case T_CON_ID:
		if (strcmp(value, "__focused__") == 0) {
			struct sway_seat *seat = input_manager_current_seat();
			struct sway_container *focus = seat_get_focused_container(seat);
			struct sway_view *view = focus ? focus->view : NULL;
			criteria->con_id = view ? view->container->node.id : 0;
		} else {
			criteria->con_id = strtoul(value, &endptr, 10);
			if (*endptr != 0) {
				error = strdup("The value for 'con_id' should be '__focused__' or numeric");
			}
		}
		break;
	case T_CON_MARK:
		pattern_create(&criteria->con_mark, value);
		break;
#if HAVE_XWAYLAND
	case T_CLASS:
		pattern_create(&criteria->class, value);
		break;
	case T_ID:
		criteria->id = strtoul(value, &endptr, 10);
		if (*endptr != 0) {
			error = strdup("The value for 'id' should be numeric");
		}
		break;
	case T_INSTANCE:
		pattern_create(&criteria->instance, value);
		break;
	case T_WINDOW_ROLE:
		pattern_create(&criteria->window_role, value);
		break;
	case T_WINDOW_TYPE:
		criteria->window_type = parse_window_type(value);
		break;
#endif
	case T_FLOATING:
		criteria->floating = true;
		break;
	case T_TILING:
		criteria->tiling = true;
		break;
	case T_URGENT:
		if (strcmp(value, "latest") == 0 ||
				strcmp(value, "newest") == 0 ||
				strcmp(value, "last") == 0 ||
				strcmp(value, "recent") == 0) {
			criteria->urgent = 'l';
		} else if (strcmp(value, "oldest") == 0 ||
				strcmp(value, "first") == 0) {
			criteria->urgent = 'o';
		} else {
			error =
				strdup("The value for 'urgent' must be 'first', 'last', "
						"'latest', 'newest', 'oldest' or 'recent'");
		}
		break;
	case T_WORKSPACE:
		pattern_create(&criteria->workspace, value);
		break;
	case T_PID:
		criteria->pid = strtoul(value, &endptr, 10);
		if (*endptr != 0) {
			error = strdup("The value for 'pid' should be numeric");
		}
		break;
	case T_INVALID:
		break;
	}

	if (error) {
		return false;
	}

	return true;
}

static void skip_spaces(char **head) {
	while (**head == ' ') {
		++*head;
	}
}

// Remove escaping slashes from value
static void unescape(char *value) {
	if (!strchr(value, '\\')) {
		return;
	}
	char *copy = calloc(strlen(value) + 1, 1);
	char *readhead = value;
	char *writehead = copy;
	while (*readhead) {
		if (*readhead == '\\' && *(readhead + 1) == '"') {
			// skip the slash
			++readhead;
		}
		*writehead = *readhead;
		++writehead;
		++readhead;
	}
	strcpy(value, copy);
	free(copy);
}

/**
 * Parse a raw criteria string such as [class="foo" instance="bar"] into a
 * criteria struct.
 *
 * If errors are found, NULL will be returned and the error argument will be
 * populated with an error string. It is up to the caller to free the error.
 */
struct criteria *criteria_parse(char *raw, char **error_arg) {
	*error_arg = NULL;
	error = NULL;

	char *head = raw;
	skip_spaces(&head);
	if (*head != '[') {
		*error_arg = strdup("No criteria");
		return NULL;
	}
	++head;

	struct criteria *criteria = calloc(1, sizeof(struct criteria));
#if HAVE_XWAYLAND
	criteria->window_type = ATOM_LAST; // default value
#endif
	char *name = NULL, *value = NULL;
	bool in_quotes = false;

	while (*head && *head != ']') {
		skip_spaces(&head);
		// Parse token name
		char *namestart = head;
		while ((*head >= 'a' && *head <= 'z') || *head == '_') {
			++head;
		}
		name = calloc(head - namestart + 1, 1);
		if (head != namestart) {
			strncpy(name, namestart, head - namestart);
		}
		// Parse token value
		skip_spaces(&head);
		value = NULL;
		if (*head == '=') {
			++head;
			skip_spaces(&head);
			if (*head == '"') {
				in_quotes = true;
				++head;
			}
			char *valuestart = head;
			if (in_quotes) {
				while (*head && (*head != '"' || *(head - 1) == '\\')) {
					++head;
				}
				if (!*head) {
					*error_arg = strdup("Quote mismatch in criteria");
					goto cleanup;
				}
			} else {
				while (*head && *head != ' ' && *head != ']') {
					++head;
				}
			}
			value = calloc(head - valuestart + 1, 1);
			strncpy(value, valuestart, head - valuestart);
			if (in_quotes) {
				++head;
				in_quotes = false;
			}
			unescape(value);
			sway_log(SWAY_DEBUG, "Found pair: %s=%s", name, value);
		}
		if (!parse_token(criteria, name, value)) {
			*error_arg = error;
			goto cleanup;
		}
		skip_spaces(&head);
		free(name);
		free(value);
		name = NULL;
		value = NULL;
	}
	if (*head != ']') {
		*error_arg = strdup("No closing brace found in criteria");
		goto cleanup;
	}

	if (criteria_is_empty(criteria)) {
		*error_arg = strdup("Criteria is empty");
		goto cleanup;
	}

	++head;
	int len = head - raw;
	criteria->raw = calloc(len + 1, 1);
	strncpy(criteria->raw, raw, len);
	return criteria;

cleanup:
	free(name);
	free(value);
	criteria_destroy(criteria);
	return NULL;
}

bool criteria_is_equal(struct criteria *left, struct criteria *right) {
	if (left->type != right->type) {
		return false;
	}
	// XXX Only implemented for CT_NO_FOCUS for now.
	if (left->type == CT_NO_FOCUS) {
		return strcmp(left->raw, right->raw) == 0;
	}
	if (left->type == CT_COMMAND) {
		return strcmp(left->raw, right->raw) == 0
				&& strcmp(left->cmdlist, right->cmdlist) == 0;
	}
	return false;
}

bool criteria_already_exists(struct criteria *criteria) {
	// XXX Only implemented for CT_NO_FOCUS and CT_COMMAND for now.
	// While criteria_is_equal also obeys this limitation, this is a shortcut
	// to avoid processing the list.
	if (criteria->type != CT_NO_FOCUS && criteria->type != CT_COMMAND) {
		return false;
	}

	list_t *criterias = config->criteria;
	for (int i = 0; i < criterias->length; ++i) {
		struct criteria *existing = criterias->items[i];
		if (criteria_is_equal(criteria, existing)) {
			return true;
		}
	}
	return false;
}
