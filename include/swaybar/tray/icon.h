#ifndef _SWAYBAR_TRAY_ICON_H
#define _SWAYBAR_TRAY_ICON_H

#include "list.h"

struct icon_theme_subdir {
	char *name;
	int size;

	enum {
		THRESHOLD,
		SCALABLE,
		FIXED
	} type;

	int max_size;
	int min_size;
	int threshold;
};

struct icon_theme {
	char *name;
	int name_locale_level;
	char *comment;
	list_t *inherits; // char *
	list_t *directories; // char *

	char *dir;
	list_t *subdirs; // struct icon_theme_subdir *
};

void init_themes(list_t **themes, list_t **basedirs);
void finish_themes(list_t *themes, list_t *basedirs);

/*
 * Finds an icon of a specified size given a list of themes and base directories.
 * If the icon is found, the pointers min_size & max_size are set to minimum &
 * maximum size that the icon can be scaled to, respectively.
 * Returns: path of icon (which should be freed), or NULL if the icon is not found.
 */
char *find_icon(list_t *themes, list_t *basedirs, char *name, int size,
		char *theme, int *min_size, int *max_size);

#endif
