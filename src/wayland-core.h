#ifndef WAYLAND_CORE_H
#define WAYLAND_CORE_H

#include "types.h"
#include <wayland-client.h>

extern const struct wl_registry_listener registry_listener;
void fetch_hyprland_colors(struct app_context *ctx);

#endif
