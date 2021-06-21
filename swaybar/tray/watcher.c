#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "log.h"
#include "swaybar/tray/watcher.h"

static const char *obj_path = "/StatusNotifierWatcher";

static bool using_standard_protocol(struct swaybar_watcher *watcher) {
	return watcher->interface[strlen("org.")] == 'f'; // freedesktop
}

static int cmp_id(const void *item, const void *cmp_to) {
	return strcmp(item, cmp_to);
}

static int handle_lost_service(sd_bus_message *msg,
		void *data, sd_bus_error *error) {
	char *service, *old_owner, *new_owner;
	int ret = sd_bus_message_read(msg, "sss", &service, &old_owner, &new_owner);
	if (ret < 0) {
		sway_log(SWAY_ERROR, "Failed to parse owner change message: %s", strerror(-ret));
		return ret;
	}

	if (!*new_owner) {
		struct swaybar_watcher *watcher = data;
		for (int idx = 0; idx < watcher->items->length; ++idx) {
			char *id = watcher->items->items[idx];
			int cmp_res = using_standard_protocol(watcher) ?
				cmp_id(id, service) : strncmp(id, service, strlen(service));
			if (cmp_res == 0) {
				sway_log(SWAY_DEBUG, "Unregistering Status Notifier Item '%s'", id);
				list_del(watcher->items, idx--);
				sd_bus_emit_signal(watcher->bus, obj_path, watcher->interface,
						"StatusNotifierItemUnregistered", "s", id);
				free(id);

				sd_bus_emit_properties_changed(watcher->bus, obj_path,
						watcher->interface, "RegisteredStatusNotifierItems", NULL);
				if (using_standard_protocol(watcher)) {
					break;
				}
			}
		}

		int idx = list_seq_find(watcher->hosts, cmp_id, service);
		if (idx != -1) {
			sway_log(SWAY_DEBUG, "Unregistering Status Notifier Host '%s'", service);
			free(watcher->hosts->items[idx]);
			list_del(watcher->hosts, idx);

			if (watcher->hosts->length == 0) {
				sd_bus_emit_properties_changed(watcher->bus, obj_path,
						watcher->interface, "IsStatusNotifierHostRegistered", NULL);
			}
		}
	}

	return 0;
}

static int register_sni(sd_bus_message *msg, void *data, sd_bus_error *error) {
	char *service_or_path, *id;
	int ret = sd_bus_message_read(msg, "s", &service_or_path);
	if (ret < 0) {
		sway_log(SWAY_ERROR, "Failed to parse register SNI message: %s", strerror(-ret));
		return ret;
	}

	struct swaybar_watcher *watcher = data;
	if (using_standard_protocol(watcher)) {
		id = strdup(service_or_path);
	} else {
		const char *service, *path;
		if (service_or_path[0] == '/') {
			service = sd_bus_message_get_sender(msg);
			path = service_or_path;
		} else {
			service = service_or_path;
			path = "/StatusNotifierItem";
		}
		size_t id_len = snprintf(NULL, 0, "%s%s", service, path) + 1;
		id = malloc(id_len);
		snprintf(id, id_len, "%s%s", service, path);
	}

	if (list_seq_find(watcher->items, cmp_id, id) == -1) {
		sway_log(SWAY_DEBUG, "Registering Status Notifier Item '%s'", id);
		list_add(watcher->items, id);
		sd_bus_emit_signal(watcher->bus, obj_path, watcher->interface,
				"StatusNotifierItemRegistered", "s", id);

		sd_bus_emit_properties_changed(watcher->bus, obj_path,
				watcher->interface, "RegisteredStatusNotifierItems", NULL);
	} else {
		sway_log(SWAY_DEBUG, "Status Notifier Item '%s' already registered", id);
		free(id);
	}

	return sd_bus_reply_method_return(msg, "");
}

static int register_host(sd_bus_message *msg, void *data, sd_bus_error *error) {
	char *service;
	int ret = sd_bus_message_read(msg, "s", &service);
	if (ret < 0) {
		sway_log(SWAY_ERROR, "Failed to parse register host message: %s", strerror(-ret));
		return ret;
	}

	struct swaybar_watcher *watcher = data;
	if (list_seq_find(watcher->hosts, cmp_id, service) == -1) {
		sway_log(SWAY_DEBUG, "Registering Status Notifier Host '%s'", service);
		list_add(watcher->hosts, strdup(service));
		sd_bus_emit_signal(watcher->bus, obj_path, watcher->interface,
				"StatusNotifierHostRegistered", "s", service);

		if (watcher->hosts->length == 1) {
			sd_bus_emit_properties_changed(watcher->bus, obj_path,
					watcher->interface, "IsStatusNotifierHostRegistered", NULL);
		}
	} else {
		sway_log(SWAY_DEBUG, "Status Notifier Host '%s' already registered", service);
	}

	return sd_bus_reply_method_return(msg, "");
}

static int get_registered_snis(sd_bus *bus, const char *obj_path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *data, sd_bus_error *error) {
	struct swaybar_watcher *watcher = data;
	list_add(watcher->items, NULL); // strv expects NULL-terminated string array
	int ret = sd_bus_message_append_strv(reply, (char **)watcher->items->items);
	list_del(watcher->items, watcher->items->length - 1);
	return ret;
}

static int is_host_registered(sd_bus *bus, const char *obj_path,
		const char *interface, const char *property, sd_bus_message *reply,
		void *data, sd_bus_error *error) {
	struct swaybar_watcher *watcher = data;
	int val = watcher->hosts->length > 0; // dbus expects int rather than bool
	return sd_bus_message_append_basic(reply, 'b', &val);
}

static const sd_bus_vtable watcher_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD_WITH_NAMES("RegisterStatusNotifierItem", "s", SD_BUS_PARAM(service),
			"", /* void */, register_sni, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD_WITH_NAMES("RegisterStatusNotifierHost", "s", SD_BUS_PARAM(service),
			"", /* void */, register_host, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as", get_registered_snis,
			0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b", is_host_registered,
			0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ProtocolVersion", "i", NULL,
			offsetof(struct swaybar_watcher, version),
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_SIGNAL_WITH_NAMES("StatusNotifierItemRegistered", "s",
			SD_BUS_PARAM(service), 0),
	SD_BUS_SIGNAL_WITH_NAMES("StatusNotifierItemUnregistered", "s",
			SD_BUS_PARAM(service), 0),
	SD_BUS_SIGNAL("StatusNotifierHostRegistered", NULL, 0),
	SD_BUS_VTABLE_END
};

struct swaybar_watcher *create_watcher(char *protocol, sd_bus *bus) {
	struct swaybar_watcher *watcher = calloc(1, sizeof(struct swaybar_watcher));
	if (!watcher) {
		return NULL;
	}

	size_t len = snprintf(NULL, 0, "org.%s.StatusNotifierWatcher", protocol) + 1;
	watcher->interface = malloc(len);
	snprintf(watcher->interface, len, "org.%s.StatusNotifierWatcher", protocol);

	sd_bus_slot *signal_slot = NULL, *vtable_slot = NULL;
	int ret = sd_bus_add_object_vtable(bus, &vtable_slot, obj_path,
			watcher->interface, watcher_vtable, watcher);
	if (ret < 0) {
		sway_log(SWAY_ERROR, "Failed to add object vtable: %s", strerror(-ret));
		goto error;
	}

	ret = sd_bus_match_signal(bus, &signal_slot, "org.freedesktop.DBus",
			"/org/freedesktop/DBus", "org.freedesktop.DBus",
			"NameOwnerChanged", handle_lost_service, watcher);
	if (ret < 0) {
		sway_log(SWAY_ERROR, "Failed to subscribe to unregistering events: %s",
				strerror(-ret));
		goto error;
	}

	ret = sd_bus_request_name(bus, watcher->interface, 0);
	if (ret < 0) {
		if (-ret == EEXIST) {
			sway_log(SWAY_DEBUG, "Failed to acquire service name '%s': "
					"another watcher is already running", watcher->interface);
		} else {
			sway_log(SWAY_ERROR, "Failed to acquire service name '%s': %s",
					watcher->interface, strerror(-ret));
		}
		goto error;
	}

	sd_bus_slot_set_floating(signal_slot, 0);
	sd_bus_slot_set_floating(vtable_slot, 0);

	watcher->bus = bus;
	watcher->hosts = create_list();
	watcher->items = create_list();
	watcher->version = 0;
	sway_log(SWAY_DEBUG, "Registered %s", watcher->interface);
	return watcher;
error:
	sd_bus_slot_unref(signal_slot);
	sd_bus_slot_unref(vtable_slot);
	destroy_watcher(watcher);
	return NULL;
}

void destroy_watcher(struct swaybar_watcher *watcher) {
	if (!watcher) {
		return;
	}
	list_free_items_and_destroy(watcher->hosts);
	list_free_items_and_destroy(watcher->items);
	free(watcher->interface);
	free(watcher);
}
