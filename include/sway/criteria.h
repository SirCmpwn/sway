#ifndef _SWAY_CRITERIA_H
#define _SWAY_CRITERIA_H

#include <pcre.h>
#include "config.h"
#include "list.h"
#include "tree/view.h"

enum criteria_type {
	CT_COMMAND                 = 1 << 0,
	CT_ASSIGN_OUTPUT           = 1 << 1,
	CT_ASSIGN_WORKSPACE        = 1 << 2,
	CT_ASSIGN_WORKSPACE_NUMBER = 1 << 3,
	CT_NO_FOCUS                = 1 << 4,
};

enum pattern_type {
	PATTERN_PCRE,
	PATTERN_FOCUSED,
};

struct pattern {
	enum pattern_type match_type;
	pcre *regex;
};

struct criteria {
	enum criteria_type type;
	char *raw; // entire criteria string (for logging)
	char *cmdlist;
	char *target; // workspace or output name for `assign` criteria

	struct pattern *title;
	struct pattern *shell;
	struct pattern *app_id;
	struct pattern *con_mark;
	struct pattern *cli_label;
	uint32_t con_id; // internal ID
#if HAVE_XWAYLAND
	struct pattern *class;
	uint32_t id; // X11 window ID
	struct pattern *instance;
	struct pattern *window_role;
	enum atom_name window_type;
#endif
	bool floating;
	bool tiling;
	char urgent; // 'l' for latest or 'o' for oldest
	struct pattern *workspace;
	pid_t pid;
};

bool criteria_is_empty(struct criteria *criteria);
bool criteria_is_equal(struct criteria *left, struct criteria *right);

bool criteria_already_exists(struct criteria *criteria);

void criteria_destroy(struct criteria *criteria);

/**
 * Generate a criteria struct from a raw criteria string such as
 * [class="foo" instance="bar"] (brackets inclusive).
 *
 * The error argument is expected to be an address of a null pointer. If an
 * error is encountered, the function will return NULL and the pointer will be
 * changed to point to the error string. This string should be freed afterwards.
 */
struct criteria *criteria_parse(char *raw, char **error);

/**
 * Compile a list of criterias matching the given view.
 *
 * Criteria types can be bitwise ORed.
 */
list_t *criteria_for_view(struct sway_view *view, enum criteria_type types);

/**
 * Compile a list of containers matching the given criteria.
 */
list_t *criteria_get_containers(struct criteria *criteria);

#endif
