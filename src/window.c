#include "window.h"
#include "buffer.h"



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
