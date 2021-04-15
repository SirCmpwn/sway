#ifndef _SWAY_INPUT_LIBINPUT_H
#define _SWAY_INPUT_LIBINPUT_H
#include "sway/input/input-manager.h"

void sway_input_configure_libinput_device(struct sway_input_device *device);

void sway_input_reset_libinput_device(struct sway_input_device *device);

bool sway_libinput_device_is_builtin(struct sway_input_device *device);

#endif
