#define _GNU_SOURCE

#include "basics.h"
#include "types.h"
#include "wayland-core.h"
#include "window.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>



uint32_t setup(struct app_context *ctx) {
    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) return 1;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->layer_shell || !ctx->shm) {
        fprintf(stderr, "Err: crittical Wayland handler missing.\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }

    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlauncher");

    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, anchors);
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, ctx->height);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, ctx->height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, 0);
    zwlr_layer_surface_v1_add_listener(ctx->layer_surface, &layer_surface_listener, ctx);

    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);
    return 0;
}



void destroy(struct app_context *ctx) {
    if (ctx->surface) {
        wl_surface_attach(ctx->surface, NULL, 0, 0);
        wl_surface_commit(ctx->surface);
    }
    wl_display_flush(ctx->display);

    if (ctx->display)       wl_display_roundtrip(ctx->display);
    if (ctx->seat)          wl_seat_destroy(ctx->seat);
    if (ctx->layer_surface) zwlr_layer_surface_v1_destroy(ctx->layer_surface);
    if (ctx->surface)       wl_surface_destroy(ctx->surface);
    if (ctx->buffer)        wl_buffer_destroy(ctx->buffer);
    if (ctx->display)       wl_display_disconnect(ctx->display);
}



int main(int argc, char **argv) {

    if (checkIfRunning()) return 0;
    zombieProtect();

    if (!optHandling(argc, argv)) return 0;

    struct app_context stack_ctx;
    register struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));
    ctx->running = 1;
    ctx->width = 0;
    ctx->height = 24;

    fetch_hyprland_colors(ctx);

    if (setup(ctx)) return 0;


    while (ctx->running && wl_display_dispatch(ctx->display) != -1) {}


    destroy(ctx);

    return 0;
}
