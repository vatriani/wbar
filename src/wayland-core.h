#ifndef WAYLAND_CORE_H
#define WAYLAND_CORE_H

#include "types.h"
#include <wayland-client.h>

extern const struct wl_seat_listener seat_listener;
extern const struct wl_registry_listener registry_listener;
void fetch_hyprland_colors(struct app_context *ctx);
char *query_hyprland_ipc(const char *command);
int get_socket_path(char *path, size_t max_len);
void send_hyprland_cmd(const char *command);

#endif
