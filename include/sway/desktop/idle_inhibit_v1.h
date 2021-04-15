#ifndef _SWAY_DESKTOP_IDLE_INHIBIT_V1_H
#define _SWAY_DESKTOP_IDLE_INHIBIT_V1_H
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle.h>
#include "sway/server.h"

enum sway_idle_inhibit_mode {
	INHIBIT_IDLE_APPLICATION,  // Application set inhibitor (when visible)
	INHIBIT_IDLE_FOCUS,  // User set inhibitor when focused
	INHIBIT_IDLE_FULLSCREEN,  // User set inhibitor when fullscreen + visible
	INHIBIT_IDLE_OPEN,  // User set inhibitor while open
	INHIBIT_IDLE_VISIBLE  // User set inhibitor when visible
};

struct sway_idle_inhibit_manager_v1 {
	struct wlr_idle_inhibit_manager_v1 *wlr_manager;
	struct wl_listener new_idle_inhibitor_v1;
	struct wl_list inhibitors;

	struct wlr_idle *idle;
};

struct sway_idle_inhibitor_v1 {
	struct sway_idle_inhibit_manager_v1 *manager;
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor;
	struct sway_view *view;
	enum sway_idle_inhibit_mode mode;

	struct wl_list link;
	struct wl_listener destroy;
};

bool sway_idle_inhibit_v1_is_active(
	struct sway_idle_inhibitor_v1 *inhibitor);

void sway_idle_inhibit_v1_check_active(
	struct sway_idle_inhibit_manager_v1 *manager);

void sway_idle_inhibit_v1_user_inhibitor_register(struct sway_view *view,
		enum sway_idle_inhibit_mode mode);

struct sway_idle_inhibitor_v1 *sway_idle_inhibit_v1_user_inhibitor_for_view(
		struct sway_view *view);

struct sway_idle_inhibitor_v1 *sway_idle_inhibit_v1_application_inhibitor_for_view(
		struct sway_view *view);

void sway_idle_inhibit_v1_user_inhibitor_destroy(
		struct sway_idle_inhibitor_v1 *inhibitor);

struct sway_idle_inhibit_manager_v1 *sway_idle_inhibit_manager_v1_create(
	struct wl_display *wl_display, struct wlr_idle *idle);
#endif
