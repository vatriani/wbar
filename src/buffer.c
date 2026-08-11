#define _GNU_SOURCE

#include "buffer.h"
#include "types.h"
#include "sys-vitals.h"

#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-client.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <string.h>





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
    if (!ctx || !ctx->shm || !ctx->surface) return;

    if (ctx->height != 16) ctx->height = 16;
    if (ctx->width <= 0) ctx->width = 1920;

    int stride = ctx->width * 4;
    int size = stride * ctx->height;

    int fd = allocate_shm_file(size);
    if (fd < 0) return;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    memset(data, 0, size);

    cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)data, CAIRO_FORMAT_RGB24, ctx->width, ctx->height, stride
    );
    cairo_t *cr = cairo_create(cairo_surface);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgb(cr, ctx->bg_r, ctx->bg_g, ctx->bg_b);
    cairo_paint(cr);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font_desc = pango_font_description_from_string(ctx->font);
    pango_layout_set_font_description(layout, font_desc);

    int sorted_ids[32];
    int sorted_windows[32];
    memcpy(sorted_ids, ctx->workspaces, sizeof(sorted_ids));
    memcpy(sorted_windows, ctx->workspace_windows, sizeof(sorted_windows));

    for (int i = 0; i < ctx->workspace_count - 1; ++i) {
        for (int j = 0; j < ctx->workspace_count - i - 1; ++j) {
            if (sorted_ids[j] > sorted_ids[j + 1]) {
                int temp_id = sorted_ids[j];
                sorted_ids[j] = sorted_ids[j + 1];
                sorted_ids[j + 1] = temp_id;

                int temp_win = sorted_windows[j];
                sorted_windows[j] = sorted_windows[j + 1];
                sorted_windows[j + 1] = temp_win;
            }
        }
    }

    int current_x = 15;
    int text_width = 0, text_height = 0;

    for (int i = 0; i < ctx->workspace_count; ++i) {
        char item[128];
        char id_str[32];
        snprintf(id_str, sizeof(id_str), "%d", sorted_ids[i]);

        if (sorted_windows[i] > 0) {
            snprintf(item, sizeof(item), " [%d] - %d ", sorted_ids[i], sorted_windows[i]);
        } else {
            snprintf(item, sizeof(item), " [%d] ", sorted_ids[i]);
        }

        pango_layout_set_text(layout, item, -1);
        pango_layout_get_pixel_size(layout, &text_width, &text_height);
        int is_active = (ctx->active_workspace && strcmp(ctx->active_workspace, id_str) == 0);

        if (is_active) {
            cairo_set_source_rgb(cr, ctx->accent_r, ctx->accent_g, ctx->accent_b);
            cairo_rectangle(cr, current_x, 0, text_width, ctx->height);
            cairo_fill(cr);
        }

        cairo_set_source_rgb(cr, ctx->fg_r, ctx->fg_g, ctx->fg_b);

        cairo_move_to(cr, current_x, (ctx->height - text_height) / 2);
        pango_cairo_show_layout(cr, layout);
        current_x += text_width;

        if (i < ctx->workspace_count - 1) {
            pango_layout_set_text(layout, " | ", -1);
            pango_layout_get_pixel_size(layout, &text_width, &text_height);

            cairo_set_source_rgb(cr, ctx->fg_r * 0.6, ctx->fg_g * 0.6, ctx->fg_b * 0.6);
            cairo_move_to(cr, current_x, (ctx->height - text_height) / 2);
            pango_cairo_show_layout(cr, layout);
            current_x += text_width;
        }
    }

    pango_font_description_free(font_desc);
    g_object_unref(layout);

    // 5. AKTIVE APP IN DER MITTE ZEICHNEN
    if (ctx->active_app && ctx->active_app[0] != '\0') {
        char app_clean[256];
        strncpy(app_clean, ctx->active_app, sizeof(app_clean) - 1);
        app_clean[sizeof(app_clean) - 1] = '\0';

        char *comma = strchr(app_clean, ',');
        if (comma) {
            size_t title_len = strlen(comma + 1) + 1;
            memmove(app_clean, comma + 1, title_len);
        }

        PangoLayout *app_layout = pango_cairo_create_layout(cr);
        PangoFontDescription *app_font = pango_font_description_from_string(ctx->font);
        pango_layout_set_font_description(app_layout, app_font);
        pango_layout_set_text(app_layout, app_clean, -1);
        pango_layout_get_pixel_size(app_layout, &text_width, &text_height);

        int middle_x = (ctx->width - text_width) / 2;

        cairo_set_source_rgb(cr, ctx->fg_r, ctx->fg_g, ctx->fg_b);
        cairo_move_to(cr, middle_x, (ctx->height - text_height) / 2);
        pango_cairo_show_layout(cr, app_layout);

        pango_font_description_free(app_font);
        g_object_unref(app_layout);
    }

    // 6. SYSTEM-INFOS RECHTSBÜNDIG ZEICHNEN
    if (ctx->sys_time[0] == '\0') {
        get_iso_time(ctx->sys_time, sizeof(ctx->sys_time));
        get_ram_usage(ctx->sys_ram, sizeof(ctx->sys_ram));
        get_battery_info(ctx->sys_bat, sizeof(ctx->sys_bat));
        ctx->sys_cpu = get_cpu_load();
    }

    char right_bar_string[256];
    if (strstr(ctx->sys_bat, "BAT N/A") != NULL) {
        snprintf(right_bar_string, sizeof(right_bar_string), "CPU: %d%% | %s | %s ",
                 ctx->sys_cpu, ctx->sys_ram, ctx->sys_time);
    } else {
        snprintf(right_bar_string, sizeof(right_bar_string), "CPU: %d%% | %s | %s | %s ",
                 ctx->sys_cpu, ctx->sys_ram, ctx->sys_bat, ctx->sys_time);
    }

    PangoLayout *sys_layout = pango_cairo_create_layout(cr);
    PangoFontDescription *sys_font = pango_font_description_from_string(ctx->font);
    pango_layout_set_font_description(sys_layout, sys_font);
    pango_layout_set_text(sys_layout, right_bar_string, -1);

    int sys_width = 0, sys_height = 0;
    pango_layout_get_pixel_size(sys_layout, &sys_width, &sys_height);

    int right_x = ctx->width - sys_width - 15;

    cairo_set_source_rgb(cr, ctx->fg_r * 0.6, ctx->fg_g * 0.6, ctx->fg_b * 0.6);
    cairo_move_to(cr, right_x, (ctx->height - sys_height) / 2);
    pango_cairo_show_layout(cr, sys_layout);

    pango_font_description_free(sys_font);
    g_object_unref(sys_layout);

    cairo_destroy(cr);
    cairo_surface_destroy(cairo_surface);

    // 6. Wayland Buffer-Zuweisung an die Surface
    if (ctx->buffer) {
        wl_buffer_destroy(ctx->buffer);
        ctx->buffer = NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    struct wl_buffer *next_buffer = wl_shm_pool_create_buffer(pool, 0, ctx->width, ctx->height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    if (next_buffer) {
        wl_buffer_add_listener(next_buffer, &buffer_listener, ctx);
        ctx->buffer = next_buffer;
        wl_surface_attach(ctx->surface, ctx->buffer, 0, 0);
        wl_surface_damage_buffer(ctx->surface, 0, 0, ctx->width, ctx->height);
        wl_surface_damage(ctx->surface, 0, 0, ctx->width, ctx->height);
        wl_surface_commit(ctx->surface);
    }

    munmap(data, size);
}
