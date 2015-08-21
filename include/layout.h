#ifndef _SWAY_LAYOUT_H
#define _SWAY_LAYOUT_H

#include <wlc/wlc.h>
#include "list.h"
#include "container.h"
#include "focus.h"

extern swayc_t root_container;

void init_layout(void);

void add_child(swayc_t *parent, swayc_t *child);
void add_floating(swayc_t *ws, swayc_t *child);
// Returns parent container which needs to be rearranged.
swayc_t *add_sibling(swayc_t *sibling, swayc_t *child);
swayc_t *replace_child(swayc_t *child, swayc_t *new_child);
swayc_t *remove_child(swayc_t *child);

// Layout
void arrange_windows(swayc_t *container, int width, int height);

// Focus
void unfocus_all(swayc_t *container);
void focus_view(swayc_t *view);
void focus_view_for(swayc_t *ancestor, swayc_t *container);

#endif
