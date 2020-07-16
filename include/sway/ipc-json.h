#ifndef _SWAY_IPC_JSON_H
#define _SWAY_IPC_JSON_H
#include <json.h>
#include "sway/tree/container.h"
#include "sway/input/input-manager.h"

json_object *ipc_json_get_version(void);

json_object *ipc_json_get_binding_mode(void);
json_object *ipc_json_describe_binding(struct sway_binding *binding);
json_object *ipc_json_describe_binding_mode(struct sway_mode *mode);

json_object *ipc_json_describe_disabled_output(struct sway_output *o);
json_object *ipc_json_describe_node(struct sway_node *node);
json_object *ipc_json_describe_node_recursive(struct sway_node *node);
json_object *ipc_json_describe_input(struct sway_input_device *device);
json_object *ipc_json_describe_seat(struct sway_seat *seat);
json_object *ipc_json_describe_bar_config(struct bar_config *bar);

#endif
