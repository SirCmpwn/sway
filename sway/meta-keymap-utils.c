/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Utilities for use with libxkbcommon
 *
 * Copyright 2019 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "meta-keymap-utils.h"

#include <glib.h>
#include <linux/limits.h>

struct xkb_context *
meta_create_xkb_context (void)
{
	struct xkb_context *ctx;
	char xdg[PATH_MAX] = {0};
	const char *env;

	/*
	 * We can only append search paths in libxkbcommon, so we start with an
	 * emtpy set, then add the XDG dir, then add the default search paths.
	 */
	ctx = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES);

	env = g_getenv("XDG_CONFIG_HOME");
	if (env) {
		g_snprintf(xdg, sizeof xdg, "%s/xkb", env);
	}
	else if ((env = g_getenv("HOME"))) {
		g_snprintf(xdg, sizeof xdg, "%s/.config/xkb", env);
	}

	if (env) {
		xkb_context_include_path_append(ctx, xdg);
	}
	xkb_context_include_path_append_default(ctx);

	return ctx;
}
