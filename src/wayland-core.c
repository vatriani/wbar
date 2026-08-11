#include "wayland-core.h"
#include "buffer.h"
#include "types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/un.h>
#include <wordexp.h>
#include <ctype.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>



static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    (void)data; (void)seat; (void)capabilities;
}



static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}


const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};



void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    struct app_context *ctx = (struct app_context *)data;
    if (!ctx) return;

    if (strcmp(interface, "wl_compositor") == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, bind_ver);
    } else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, bind_ver);
    } else if (strcmp(interface, "wl_shm") == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, bind_ver);
    } else if (strcmp(interface, "wl_seat") == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->seat = wl_registry_bind(registry, id, &wl_seat_interface, bind_ver);
    }
}



void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}



const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};
