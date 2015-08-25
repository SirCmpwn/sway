#ifndef _SWAY_LAYOUT_H
#define _SWAY_LAYOUT_H

#include <wlc/wlc.h>
#include "list.h"
#include "container.h"
#include "focus.h"

extern swayc_t root_container;

extern int min_sane_w;
extern int min_sane_h;

void init_layout(void);

void add_child(swayc_t *parent, swayc_t *child);
void add_floating(swayc_t *ws, swayc_t *child);
// Returns parent container which needs to be rearranged.
swayc_t *add_sibling(swayc_t *sibling, swayc_t *child);
swayc_t *replace_child(swayc_t *child, swayc_t *new_child);
swayc_t *remove_child(swayc_t *child);
void swap_container(swayc_t *a, swayc_t *b);

void move_container(swayc_t* container,swayc_t* root,enum movement_direction direction);

// Layout
void update_geometry(swayc_t *view);
void arrange_windows(swayc_t *container, double width, double height);
void order_windows(swayc_t *container);

swayc_t *get_swayc_in_direction(swayc_t *container, enum movement_direction dir);
swayc_t *get_swayc_in_direction_under(swayc_t *container, enum movement_direction dir, swayc_t *limit);

void recursive_resize(swayc_t *container, double amount, enum wlc_resize_edge edge);

#endif
