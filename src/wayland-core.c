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



static void seat_handle_capabilities(void *data, struct wl_seat *seat,
        uint32_t capabilities) {
    (void)data; (void)seat; (void)capabilities;
}



static void seat_handle_name(void *data, struct wl_seat *seat,
        const char *name) {
    (void)data; (void)seat; (void)name;
}



const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};



static void output_handle_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
    (void)data; (void)wl_output; (void)x; (void)y; (void)physical_width;
    (void)physical_height; (void)subpixel; (void)make; (void)model;
    (void)transform;
}



static void output_handle_mode(void *data, struct wl_output *wl_output,
        uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    (void)data; (void)wl_output; (void) height; (void)refresh;

    struct app_context *ctx = data;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        if (ctx->wl.width <= 0) {
            ctx->wl.width = width;
        }
    }
}



static void output_handle_done(void *data, struct wl_output *wl_output) {
    (void)data; (void)wl_output;
}



static void output_handle_scale(void *data, struct wl_output *wl_output,
        int32_t factor) {
    (void)data; (void)wl_output; (void)factor;
}



static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
};



void registry_handle_global(void *data, struct wl_registry *registry,
        uint32_t id, const char *interface, uint32_t version) {
    struct app_context *ctx = (struct app_context *)data;
    if (!ctx) return;

    if (strcmp(interface, "wl_compositor") == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->wl.compositor = wl_registry_bind(registry, id,
                &wl_compositor_interface, bind_ver);
    }
    else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->wl.layer_shell = wl_registry_bind(registry, id,
                &zwlr_layer_shell_v1_interface, bind_ver);
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->wl.shm = wl_registry_bind(registry, id,
                &wl_shm_interface, bind_ver);
    }
    else if (strcmp(interface, "wl_seat") == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->wl.seat = wl_registry_bind(registry, id,
                &wl_seat_interface, bind_ver);
    }
    if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output = wl_registry_bind(registry, id,
                &wl_output_interface, 1);
        wl_output_add_listener(output, &output_listener, ctx);
    }
}



void registry_handle_global_remove(void *data, struct wl_registry *registry,
        uint32_t id) {
    (void)data; (void)registry; (void)id;
}



const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};


static void layer_surface_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
        uint32_t width, uint32_t height) {
    struct app_context *ctx = (struct app_context *)data;

    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    if (width > 0) ctx->wl.width = width;
    else if (ctx->wl.width <= 0)  ctx->wl.width = 1920;

    if (height > 0) ctx->wl.height = height;

    int is_first_time = (ctx->wl.configured == 0);
    ctx->wl.configured = 1;

    if (is_first_time && ctx->wl.width > 0 && ctx->wl.height > 0) draw_frame(ctx);
}


static void layer_surface_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    register struct app_context *ctx = (struct app_context *)data;
    (void)layer_surface;
    ctx->running = 0;
}



const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};
