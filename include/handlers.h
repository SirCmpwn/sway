#ifndef _SWAY_HANDLERS_H
#define _SWAY_HANDLERS_H
#include "container.h"
#include <stdbool.h>
#include <wlc/wlc.h>

extern struct wlc_interface interface;
extern struct wlc_origin mouse_origin;
extern uint32_t keys_pressed[32];

// set focus to current pointer location and return focused container
swayc_t *container_under_pointer(void);

#endif
