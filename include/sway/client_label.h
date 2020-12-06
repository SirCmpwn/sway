#include <wayland-server-core.h>

char* wl_client_label_get(struct wl_client *client);
void wl_client_label_set(struct wl_client *client, char* label);
