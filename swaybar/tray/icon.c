#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wordexp.h>
#include "swaybar/tray/icon.h"
#include "config.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

static int cmp_id(const void *item, const void *cmp_to) {
	return strcmp(item, cmp_to);
}

static bool dir_exists(char *path) {
	struct stat sb;
	return stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
}

static list_t *get_basedirs(void) {
	list_t *basedirs = create_list();
	list_add(basedirs, strdup("$HOME/.icons")); // deprecated

	char *data_home = getenv("XDG_DATA_HOME");
	list_add(basedirs, strdup(data_home && *data_home ?
			"$XDG_DATA_HOME/icons" : "$HOME/.local/share/icons"));

	list_add(basedirs, strdup("/usr/share/pixmaps"));

	char *data_dirs = getenv("XDG_DATA_DIRS");
	if (!(data_dirs && *data_dirs)) {
		data_dirs = "/usr/local/share:/usr/share";
	}
	data_dirs = strdup(data_dirs);
	char *dir = strtok(data_dirs, ":");
	do {
		size_t path_len = snprintf(NULL, 0, "%s/icons", dir) + 1;
		char *path = malloc(path_len);
		snprintf(path, path_len, "%s/icons", dir);
		list_add(basedirs, path);
	} while ((dir = strtok(NULL, ":")));
	free(data_dirs);

	list_t *basedirs_expanded = create_list();
	for (int i = 0; i < basedirs->length; ++i) {
		wordexp_t p;
		if (wordexp(basedirs->items[i], &p, WRDE_UNDEF) == 0) {
			if (dir_exists(p.we_wordv[0])) {
				list_add(basedirs_expanded, strdup(p.we_wordv[0]));
			}
			wordfree(&p);
		}
	}

	list_free_items_and_destroy(basedirs);

	return basedirs_expanded;
}

static void destroy_theme(struct icon_theme *theme) {
	if (!theme) {
		return;
	}
	free(theme->name);
	free(theme->comment);
	list_free_items_and_destroy(theme->inherits);
	list_free_items_and_destroy(theme->directories);
	free(theme->dir);

	for (int i = 0; i < theme->subdirs->length; ++i) {
		struct icon_theme_subdir *subdir = theme->subdirs->items[i];
		free(subdir->name);
		free(subdir);
	}
	list_free(theme->subdirs);
	free(theme);
}

static const char *group_handler(char *old_group, char *new_group,
		struct icon_theme *theme) {
	if (!old_group) {
		return new_group && strcmp(new_group, "Icon Theme") == 0 ? NULL :
			"first group must be 'Icon Theme'";
	}

	if (strcmp(old_group, "Icon Theme") == 0) {
		if (!theme->name) {
			return "missing required key 'Name'";
		} else if (!theme->comment) {
			return "missing required key 'Comment'";
		} else if (!theme->directories) {
			return "missing required key 'Directories'";
		}
	} else {
		if (theme->subdirs->length == 0) { // skip
			return NULL;
		}

		struct icon_theme_subdir *subdir =
			theme->subdirs->items[theme->subdirs->length - 1];
		if (!subdir->size) {
			return "missing required key 'Size'";
		}

		switch (subdir->type) {
		case FIXED: subdir->max_size = subdir->min_size = subdir->size;
			break;
		case SCALABLE: {
			if (!subdir->max_size) subdir->max_size = subdir->size;
			if (!subdir->min_size) subdir->min_size = subdir->size;
			break;
		}
		case THRESHOLD:
			subdir->max_size = subdir->size + subdir->threshold;
			subdir->min_size = subdir->size - subdir->threshold;
		}
	}

	if (new_group && list_seq_find(theme->directories, cmp_id, new_group) != -1) {
		struct icon_theme_subdir *subdir = calloc(1, sizeof(struct icon_theme_subdir));
		if (!subdir) {
			return "out of memory";
		}
		subdir->name = strdup(new_group);
		subdir->threshold = 2;
		list_add(theme->subdirs, subdir);
	}

	return NULL;
}

static const char *entry_handler(char *group, char *key, char *value,
		int locale_level, struct icon_theme *theme) {
	if (strcmp(group, "Icon Theme") == 0) {
		if (strcmp(key, "Name") == 0) {
			if (locale_level > theme->name_locale_level) {
				for (char *c = value; *c; ++c) {
					if (iscntrl(*c)) {
						return "malformed theme name";
					}
				}
				theme->name = strdup(value);
				theme->name_locale_level = locale_level;
			}
		} else if (strcmp(key, "Comment") == 0) {
			if (!theme->comment && locale_level >= 0) {
				theme->comment = strdup(value);
			}
		} else if (strcmp(key, "Inherits") == 0) {
			theme->inherits = split_string(value, ",");
		} else if (strcmp(key, "Directories") == 0) {
			theme->directories = split_string(value, ",");
		} // Ignored: ScaledDirectories, Hidden, Example
	} else {
		if (theme->subdirs->length == 0) { // skip
			return NULL;
		}

		struct icon_theme_subdir *subdir =
			theme->subdirs->items[theme->subdirs->length - 1];
		if (strcmp(subdir->name, group) != 0) { // skip
			return NULL;
		}

		if (strcmp(key, "Context") == 0) {
			return NULL; // ignored, but explicitly handled to not fail parsing
		} else if (strcmp(key, "Type") == 0) {
			if (strcmp(value, "Fixed") == 0) {
				subdir->type = FIXED;
			} else if (strcmp(value, "Scalable") == 0) {
				subdir->type = SCALABLE;
			} else if (strcmp(value, "Threshold") == 0) {
				subdir->type = THRESHOLD;
			} else {
				return "invalid value - expected 'Fixed', 'Scalable' or 'Threshold'";
			}
			return NULL;
		}

		char *end;
		int n = strtol(value, &end, 10);
		if (*end != '\0') {
			return "invalid value - expected a number";
		}

		if (strcmp(key, "Size") == 0) {
			subdir->size = n;
		} else if (strcmp(key, "MaxSize") == 0) {
			subdir->max_size = n;
		} else if (strcmp(key, "MinSize") == 0) {
			subdir->min_size = n;
		} else if (strcmp(key, "Threshold") == 0) {
			subdir->threshold = n;
		} // Ignored: Scale
	}
	return NULL;
}

// TODO make this better?
// The C standard claims that setlocale returns an opaque string, but doesn't
// provide any mechanisms for retrieving useful information from the string
void get_locale_matchings(char *locale_matchings[6]) {
	char *locale_ref = setlocale(LC_MESSAGES, NULL);
	if (locale_ref == NULL || strcmp(locale_ref, "") == 0 ||
			strcmp(locale_ref, "C") == 0 || strcmp(locale_ref, "POSIX") == 0) {
		return;
	}

	// duplicate locale without encoding
	char *locale = strdup(locale_ref);
	char *encoding = strchr(locale, '.');
	char *modifier = strchr(locale_ref, '@');
	if (encoding) {
		if (modifier) {
			modifier = strcpy(encoding, modifier);
		} else {
			*encoding = '\0';
		}
	}

	char *country = strchr(locale, '_');
	if (modifier && country) {
		locale_matchings[5] = locale;
		locale_matchings[4] = strndup(locale, modifier - locale);
		locale_matchings[3] = strdup(locale);
		strcpy(locale_matchings[3] + (country - locale), modifier);
		locale_matchings[2] = strndup(locale, country - locale);
	} else if (country) {
		locale_matchings[4] = locale;
		locale_matchings[2] = strndup(locale, country - locale);
	} else if (modifier) {
		locale_matchings[3] = locale;
		locale_matchings[2] = strndup(locale, modifier - locale);
	} else {
		locale_matchings[2] = locale;
	}

	for (int i = 0; i < 6; ++i) {
		sway_log(SWAY_INFO, "%d %s", i, locale_matchings[i]);
	}
}

/*
 * This is a Freedesktop Desktop Entry parser (essentially INI)
 * It calls entry_handler for every entry
 * and group_handler between every group (as well as at both ends)
 * Handlers return whether an error occurred, which stops parsing
 */
static struct icon_theme *read_theme_file(char *basedir, char *theme_name) {
	// look for index.theme file
	size_t path_len = snprintf(NULL, 0, "%s/%s/index.theme", basedir,
			theme_name) + 1;
	char *path = malloc(path_len);
	if (!path) {
		return NULL;
	}
	snprintf(path, path_len, "%s/%s/index.theme", basedir, theme_name);
	FILE *theme_file = fopen(path, "r");
	free(path);
	if (!theme_file) {
		return NULL;
	}

	struct icon_theme *theme = calloc(1, sizeof(struct icon_theme));
	if (!theme) {
		fclose(theme_file);
		return NULL;
	}
	theme->subdirs = create_list();

	// parse locale
	char *locale_matchings[6] = { NULL };
	get_locale_matchings(locale_matchings);

	list_t *groups = create_list();
	list_t *entries = create_list(); // per group

	const char *error = NULL;
	int line_no = 0;
	char *full_line = NULL;
	size_t full_len = 0;
	ssize_t nread;
	while ((nread = getline(&full_line, &full_len, theme_file)) != -1) {
		++line_no;

		char *line = full_line - 1;
		while (isspace(*++line)) {} // remove leading whitespace
		if (!*line || line[0] == '#') continue; // ignore blank lines & comments

		int len = nread - (line - full_line);
		while (isspace(line[--len])) {}
		line[++len] = '\0'; // remove trailing whitespace

		if (line[0] == '[') { // group header
			// check well-formed
			int i = 1;
			for (; !iscntrl(line[i]) && line[i] != '[' && line[i] != ']'; ++i) {}
			if (i != --len || line[i] != ']') {
				error = "malformed group header";
				break;
			}

			line[len] = '\0';

			// check group is not duplicate
			if (list_seq_find(groups, cmp_id, &line[1]) != -1) {
				error = "duplicate group";
				break;
			}

			// call handler
			char *last_group = groups->length > 0 ? groups->items[groups->length - 1] : NULL;
			error = group_handler(last_group, &line[1], theme);
			if (error) {
				break;
			}

			list_add(groups, strdup(&line[1]));
			for (int i = 0; i < entries->length; ++i) {
				free(entries->items[i]);
			}
			entries->length = 0;
		} else { // key-value pair
			if (groups->length == 0) {
				error = "unexpected content before first header";
				break;
			}

			// check well-formed
			char *p = line;
			for (; isalnum(*p) || *p == '-'; ++p) {}

			int locale_level = 1;
			if (*p == '[') {
				*p = '\0'; // remove locale from key
				char *locale = p + 1;

				while (*++p != ']') {
					if (*p == '\0') {
						error = "malformed key-value pair";
						break;
					}
				}
				if (error) {
					break;
				}

				*p++ = '\0'; // split into key-value pair

				// match locale
				for (locale_level = sizeof(locale_matchings) / sizeof(locale_matchings[0]);
						--locale_level >= 0;) {
					if (locale_matchings[locale_level] &&
						strcmp(locale, locale_matchings[locale_level]) == 0) {
						break;
					}
				}

				list_add(entries, strdup(line));
			}

			for (; isspace(*p); ++p) {}
			if (*p != '=') {
				sway_log(SWAY_INFO, "%s", p);
				error = "malformed key-value pair";
				break;
			}
			*p = '\0'; // split into key-value pair

			while (isspace(*++p)) {}
			// TODO unescape value

			// sway_log(SWAY_INFO, "%s %s %d", line, p, locale_level);
			error = entry_handler(groups->items[groups->length - 1], line, p,
					locale_level, theme);
			if (error) {
				break;
			}
		}
	}

	if (!error) {
		if (groups->length > 0) {
			error = group_handler(groups->items[groups->length - 1], NULL, theme);
		} else {
			error = "empty file";
		}
	}

	if (!error) {
		theme->dir = strdup(theme_name);
	} else {
		char *last_group = groups->length > 0 ? groups->items[groups->length-1] : "n/a";
		sway_log(SWAY_DEBUG, "Failed to load theme '%s' - parsing of file "
				"'%s/%s/index.theme' failed on line %d (group '%s'): %s",
				theme_name, basedir, theme_name, line_no, last_group, error);
		destroy_theme(theme);
		theme = NULL;
	}

	for (size_t i = 0; i < sizeof(locale_matchings) / sizeof(locale_matchings[0]); ++i) {
		free(locale_matchings[i]);
	}

	free(full_line);
	list_free_items_and_destroy(entries);
	list_free_items_and_destroy(groups);
	fclose(theme_file);
	return theme;
}

static list_t *load_themes_in_dir(char *basedir) {
	DIR *dir;
	if (!(dir = opendir(basedir))) {
		return NULL;
	}

	list_t *themes = create_list();
	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == '.') continue;

		struct icon_theme *theme = read_theme_file(basedir, entry->d_name);
		if (theme) {
			list_add(themes, theme);
		}
	}
	closedir(dir);
	return themes;
}

static void log_loaded_themes(list_t *themes) {
	if (themes->length == 0) {
		sway_log(SWAY_INFO, "Warning: no icon themes loaded");
		return;
	}

	const char sep[] = ", ";
	size_t sep_len = strlen(sep);

	size_t len = 0;
	for (int i = 0; i < themes->length; ++i) {
		struct icon_theme *theme = themes->items[i];
		len += strlen(theme->name) + sep_len;
	}

	char *str = malloc(len + 1);
	if (!str) {
		return;
	}
	char *p = str;
	for (int i = 0; i < themes->length; ++i) {
		if (i > 0) {
			memcpy(p, sep, sep_len);
			p += sep_len;
		}

		struct icon_theme *theme = themes->items[i];
		size_t name_len = strlen(theme->name);
		memcpy(p, theme->name, name_len);
		p += name_len;
	}
	*p = '\0';

	sway_log(SWAY_DEBUG, "Loaded icon themes: %s", str);
	free(str);
}

void init_themes(list_t **themes, list_t **basedirs) {
	*basedirs = get_basedirs();

	*themes = create_list();
	for (int i = 0; i < (*basedirs)->length; ++i) {
		list_t *dir_themes = load_themes_in_dir((*basedirs)->items[i]);
		if (dir_themes == NULL) {
			continue;
		}
		list_cat(*themes, dir_themes);
		list_free(dir_themes);
	}

	log_loaded_themes(*themes);
}

void finish_themes(list_t *themes, list_t *basedirs) {
	for (int i = 0; i < themes->length; ++i) {
		destroy_theme(themes->items[i]);
	}
	list_free(themes);
	list_free_items_and_destroy(basedirs);
}

static char *find_icon_in_subdir(char *name, char *basedir, char *theme,
		char *subdir) {
	static const char *extensions[] = {
#if HAVE_GDK_PIXBUF
		"svg",
#endif
		"png",
#if HAVE_GDK_PIXBUF
		"xpm" // deprecated
#endif
	};

	size_t path_len = snprintf(NULL, 0, "%s/%s/%s/%s.EXT", basedir, theme,
			subdir, name) + 1;
	char *path = malloc(path_len);

	for (size_t i = 0; i < sizeof(extensions) / sizeof(*extensions); ++i) {
		snprintf(path, path_len, "%s/%s/%s/%s.%s", basedir, theme, subdir,
				name, extensions[i]);
		if (access(path, R_OK) == 0) {
			return path;
		}
	}

	free(path);
	return NULL;
}

static bool theme_exists_in_basedir(char *theme, char *basedir) {
	size_t path_len = snprintf(NULL, 0, "%s/%s", basedir, theme) + 1;
	char *path = malloc(path_len);
	snprintf(path, path_len, "%s/%s", basedir, theme);
	bool ret = dir_exists(path);
	free(path);
	return ret;
}

static char *find_icon_with_theme(list_t *basedirs, list_t *themes, char *name,
		int size, char *theme_name, int *min_size, int *max_size) {
	struct icon_theme *theme = NULL;
	for (int i = 0; i < themes->length; ++i) {
		theme = themes->items[i];
		if (strcmp(theme->name, theme_name) == 0) {
			break;
		}
		theme = NULL;
	}
	if (!theme) return NULL;

	char *icon = NULL;
	for (int i = 0; i < basedirs->length; ++i) {
		if (!theme_exists_in_basedir(theme->dir, basedirs->items[i])) {
			continue;
		}
		// search backwards to hopefully hit scalable/larger icons first
		for (int j = theme->subdirs->length - 1; j >= 0; --j) {
			struct icon_theme_subdir *subdir = theme->subdirs->items[j];
			if (size >= subdir->min_size && size <= subdir->max_size) {
				if ((icon = find_icon_in_subdir(name, basedirs->items[i],
								theme->dir, subdir->name))) {
					*min_size = subdir->min_size;
					*max_size = subdir->max_size;
					return icon;
				}
			}
		}
	}

	// inexact match
	unsigned smallest_error = -1; // UINT_MAX
	for (int i = 0; i < basedirs->length; ++i) {
		if (!theme_exists_in_basedir(theme->dir, basedirs->items[i])) {
			continue;
		}
		for (int j = theme->subdirs->length - 1; j >= 0; --j) {
			struct icon_theme_subdir *subdir = theme->subdirs->items[j];
			unsigned error = (size > subdir->max_size ? size - subdir->max_size : 0)
				+ (size < subdir->min_size ? subdir->min_size - size : 0);
			if (error < smallest_error) {
				char *test_icon = find_icon_in_subdir(name, basedirs->items[i],
						theme->dir, subdir->name);
				if (test_icon) {
					free(icon);
					icon = test_icon;
					smallest_error = error;
					*min_size = subdir->min_size;
					*max_size = subdir->max_size;
				}
			}
		}
	}

	if (!icon && theme->inherits) {
		for (int i = 0; i < theme->inherits->length; ++i) {
			icon = find_icon_with_theme(basedirs, themes, name, size,
					theme->inherits->items[i], min_size, max_size);
			if (icon) {
				break;
			}
		}
	}

	return icon;
}

static char *find_fallback_icon(list_t *basedirs, char *name, int *min_size,
		int *max_size) {
	for (int i = 0; i < basedirs->length; ++i) {
		char *icon = find_icon_in_subdir(name, basedirs->items[i], "", "");
		if (icon) {
			*min_size = 1;
			*max_size = 512;
			return icon;
		}
	}
	return NULL;
}

char *find_icon(list_t *themes, list_t *basedirs, char *name, int size,
		char *theme, int *min_size, int *max_size) {
	// TODO https://specifications.freedesktop.org/icon-theme-spec/icon-theme-spec-latest.html#implementation_notes
	char *icon = NULL;
	if (theme) {
		icon = find_icon_with_theme(basedirs, themes, name, size, theme,
				min_size, max_size);
	}
	if (!icon && !(theme && strcmp(theme, "Hicolor") == 0)) {
		icon = find_icon_with_theme(basedirs, themes, name, size, "Hicolor",
				min_size, max_size);
	}
	if (!icon) {
		icon = find_fallback_icon(basedirs, name, min_size, max_size);
	}
	return icon;
}
