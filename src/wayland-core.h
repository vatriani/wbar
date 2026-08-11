/**
 *  @file wayland-core.h
 *  @brief Defines some functions to encapsulate wayland connection.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */
#ifndef WAYLAND_CORE_H
#define WAYLAND_CORE_H

#include "types.h"
#include <wayland-client.h>

extern const struct wl_seat_listener seat_listener;
extern const struct wl_registry_listener registry_listener;

#endif
