#ifndef _SWAYBAR_TRAY_ITEM_H
#define _SWAYBAR_TRAY_ITEM_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "swaybar/tray/tray.h"
#include "list.h"

struct swaybar_output;

struct swaybar_pixmap {
	int size;
	unsigned char pixels[];
};

struct swaybar_sni_tool_tip {
	char *icon_name;
	list_t *icon_pixmap; // struct swaybar_pixmap *
	char *title;
	char *description; // can contain HTML subset <b><i><u><a href=""><img src=" alt="">, see https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/Markup/
};

struct swaybar_sni_slot {
	struct wl_list link; // swaybar_sni::slots
	struct swaybar_sni *sni;
	const char *prop;
	const char *type;
	void *dest;
	sd_bus_slot *slot;
};

struct swaybar_sni {
	// icon properties
	struct swaybar_tray *tray;
	cairo_surface_t *icon;
	int min_size;
	int max_size;
	int target_size;

	// dbus properties
	char *watcher_id;
	char *service;
	char *path;
	char *interface;

	char *status;
	char *icon_name;
	list_t *icon_pixmap; // struct swaybar_pixmap *
	char *attention_icon_name;
	list_t *attention_icon_pixmap; // struct swaybar_pixmap *
	bool item_is_menu;
	char *menu;
	struct swaybar_sni_tool_tip *tool_tip;
	char *title;
	char *category;
	char *id;
	char *icon_theme_path; // non-standard KDE property

	struct wl_list slots; // swaybar_sni_slot::link
};

struct swaybar_sni *create_sni(char *id, struct swaybar_tray *tray);
void destroy_sni(struct swaybar_sni *sni);
uint32_t render_sni(cairo_t *cairo, struct swaybar_output *output, double *x,
		struct swaybar_sni *sni);

#endif
