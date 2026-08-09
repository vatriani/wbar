#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-client.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <string.h>

#include "buffer.h"
#include "types.h"



static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wlauncher-shared-buffer", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}



static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    struct app_context *ctx = (struct app_context *)data;

    wl_buffer_destroy(wl_buffer);

    if (ctx && ctx->buffer == wl_buffer) {
        ctx->buffer = NULL;
    }
}



static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};



void draw_frame(struct app_context *ctx) {
    if (ctx->width <= 0 || ctx->height <= 0) return;

    int stride = ctx->width * 4;
    int size = stride * ctx->height;

    int fd = allocate_shm_file(size);
    if (fd < 0) return;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)data, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride
    );
    cairo_t *cr = cairo_create(cairo_surface);

    cairo_set_source_rgb(cr, ctx->bg_r, ctx->bg_g, ctx->bg_b);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, ctx->accent_r, ctx->accent_g, ctx->accent_b);
    cairo_rectangle(cr, 0, 0, 100, ctx->height);
    cairo_fill(cr);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, "wbar", -1);
    PangoFontDescription *font_desc = pango_font_description_from_string("DejaVu Sans Mono Bold 10");
    pango_layout_set_font_description(layout, font_desc);

    cairo_set_source_rgb(cr, ctx->bg_r, ctx->bg_g, ctx->bg_b);
    cairo_move_to(cr, 10, 3);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font_desc);

    font_desc = pango_font_description_from_string("DejaVu Sans Mono 10");
    pango_layout_set_font_description(layout, font_desc);

    pango_font_description_free(font_desc);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(cairo_surface);

    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    struct wl_buffer *next_buffer = wl_shm_pool_create_buffer(pool, 0, ctx->width, ctx->height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_buffer_add_listener(next_buffer, &buffer_listener, ctx);
    ctx->buffer = next_buffer;

    wl_surface_attach(ctx->surface, ctx->buffer, 0, 0);
    wl_surface_damage_buffer(ctx->surface, 0, 0, ctx->width, ctx->height);
    wl_surface_commit(ctx->surface);

    munmap(data, size);
}
