#include "hyprland.h"
#include "buffer.h"
#include "types.h"
#include "sys-vitals.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-client.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <string.h>



static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wbar-shared-buffer", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}



static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    (void)data; (void)wl_buffer;
}



static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};



int init_rendering(struct app_context *ctx) {
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, ctx->wl.width);
    ctx->render.shm_size = stride * ctx->wl.height;

    ctx->render.shm_fd = allocate_shm_file(ctx->render.shm_size);
    if (ctx->render.shm_fd < 0) return -1;

    ctx->render.shm_data = mmap(NULL, ctx->render.shm_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, ctx->render.shm_fd, 0);
    if (ctx->render.shm_data == MAP_FAILED) {
        close(ctx->render.shm_fd);
        ctx->render.shm_fd = -1;
        return -1;
    }
    memset(ctx->render.shm_data, 0, ctx->render.shm_size);

    ctx->render.cairo_surface_shm = cairo_image_surface_create_for_data(
        (unsigned char *)ctx->render.shm_data, CAIRO_FORMAT_RGB24,
        ctx->wl.width, ctx->wl.height, stride);
    ctx->render.cairo_t_shm = cairo_create(ctx->render.cairo_surface_shm);

    ctx->render.pango_layout = pango_cairo_create_layout(ctx->render.cairo_t_shm);
    ctx->render.pango_font_desc = pango_font_description_from_string(ctx->render.font);
    pango_layout_set_font_description(ctx->render.pango_layout, ctx->render.pango_font_desc);

    ctx->wl.pool = wl_shm_create_pool(ctx->wl.shm, ctx->render.shm_fd, ctx->render.shm_size);
    ctx->wl.buffer = wl_shm_pool_create_buffer(ctx->wl.pool,
            0, ctx->wl.width, ctx->wl.height, stride, WL_SHM_FORMAT_XRGB8888);

    if (!ctx->wl.buffer) {
        wl_shm_pool_destroy(ctx->wl.pool);
        munmap(ctx->render.shm_data, ctx->render.shm_size);
        close(ctx->render.shm_fd);
        return -1;
    }

    wl_buffer_add_listener(ctx->wl.buffer, &buffer_listener, ctx);
    ctx->changed_segments = RENDER_ALL;

    return 0;
}



void cleanup_rendering(struct app_context *ctx) {
    if (ctx->render.pango_font_desc) pango_font_description_free(ctx->render.pango_font_desc);
    if (ctx->render.pango_layout) g_object_unref(ctx->render.pango_layout);
    if (ctx->wl.buffer) wl_buffer_destroy(ctx->wl.buffer);
    if (ctx->render.shm_data) munmap(ctx->render.shm_data, ctx->render.shm_size);
    if (ctx->render.shm_fd >= 0) close(ctx->render.shm_fd);
    cairo_destroy(ctx->render.cairo_t_shm);
    cairo_surface_destroy(ctx->render.cairo_surface_shm);
}



/**
 * Vergleichsfunktion für qsort
 */
static int compare_workspaces(const void *a, const void *b) {
    const workspace *ws_a = (const workspace *)a;
    const workspace *ws_b = (const workspace *)b;

    if (ws_a->id < ws_b->id) return -1;
    if (ws_a->id > ws_b->id) return 1;
    return 0;
}


/**
 * Sortiert das Workspace-Array aufsteigend nach der Workspace-ID.
 * Nutzt qsort für optimale Performance.
 */
void sort_workspaces(workspace workspaces[MAX_WORKSPACES], int count) {
    if (!workspaces || count <= 1) return;
    qsort(workspaces, count, sizeof(workspace), compare_workspaces);
}



void segment_eraser(struct app_context *ctx, int x, int y, int width, int height) {
    cairo_rectangle(ctx->render.cairo_t_shm, x, y, width, height);
    cairo_set_source_rgb(ctx->render.cairo_t_shm, ctx->render.bg_color.r,
            ctx->render.bg_color.g, ctx->render.bg_color.b);
    cairo_fill(ctx->render.cairo_t_shm);
}



// 1. LINKS: Workspaces zeichnen
void draw_segment_left(struct app_context *ctx) {
    if (ctx->render.left_width)
        segment_eraser(ctx, 0, 0, ctx->render.left_width, ctx->wl.height);

    workspace sorted_ws[ctx->hypr.workspaces_count];
    memcpy(sorted_ws, ctx->hypr.workspaces, sizeof(workspace)*ctx->hypr.workspaces_count);
    sort_workspaces(sorted_ws, ctx->hypr.workspaces_count);


    // 3. DER EINZIGE ZEICHEN-DURCHLAUF (Direkt messen und malen)
    int current_x = ctx->render.padding;
    int text_width = 0, text_height = 0;
    int divider_width = 0, divider_height = 0;

    for (int i = 0; i < ctx->hypr.workspaces_count; ++i) {
        char item[128];
        char id_str[128];
        snprintf(id_str, sizeof(id_str), "%d", sorted_ws[i].id);

        if (sorted_ws[i].window_count > 0) {
            snprintf(item, sizeof(item), " [%d] - %d ", sorted_ws[i].id,
                    sorted_ws[i].window_count);
        } else {
            snprintf(item, sizeof(item), " [%d] ", sorted_ws[i].id);
        }

        pango_layout_set_text(ctx->render.pango_layout, item, -1);
        pango_layout_get_pixel_size(ctx->render.pango_layout, &text_width,
            &text_height);

        if (ctx->hypr.active_workspace && strcmp(ctx->hypr.active_workspace, id_str)
                == 0) {
            cairo_set_source_rgb(ctx->render.cairo_t_shm, ctx->render.accent_color.r,
                    ctx->render.accent_color.g, ctx->render.accent_color.b);
            cairo_rectangle(ctx->render.cairo_t_shm, current_x, 0, text_width,
                    ctx->wl.height);
            cairo_fill(ctx->render.cairo_t_shm);
        }

        cairo_set_source_rgb(ctx->render.cairo_t_shm, ctx->render.fg_color.r,
                ctx->render.fg_color.g, ctx->render.fg_color.b);
        cairo_move_to(ctx->render.cairo_t_shm, current_x,
                (ctx->wl.height - text_height) / 2);
        pango_cairo_show_layout(ctx->render.cairo_t_shm, ctx->render.pango_layout);
        current_x += text_width;

        if (i < ctx->hypr.workspaces_count - 1) {
            pango_layout_set_text(ctx->render.pango_layout, " | ", -1);
            if (divider_width == 0)
                pango_layout_get_pixel_size(ctx->render.pango_layout,
                        &divider_width,
                        &divider_height);

            cairo_set_source_rgb(ctx->render.cairo_t_shm, ctx->render.fg_color.r * 0.6,
                    ctx->render.fg_color.g * 0.6, ctx->render.fg_color.b * 0.6);
            cairo_move_to(ctx->render.cairo_t_shm, current_x,
                    (ctx->wl.height - text_height) / 2);
            pango_cairo_show_layout(ctx->render.cairo_t_shm, ctx->render.pango_layout);
            current_x += divider_width;
        }
    }

    ctx->render.left_width = current_x + ctx->render.padding;
}



// 2. MITTE: Aktive App zeichnen
void draw_segment_center(struct app_context *ctx) {
    if (ctx->render.center_width) {
        segment_eraser(ctx,
                (ctx->wl.width - ctx->render.center_width) / 2, 0,
                ctx->render.center_width, ctx->wl.height);
    }

     if (ctx->hypr.active_app[0] != '\0') {
        int available_width = ctx->wl.width - ctx->render.left_width -
                ctx->render.right_width - (4 * ctx->render.padding);
        pango_layout_set_single_paragraph_mode(ctx->render.pango_layout, TRUE);
        pango_layout_set_width(ctx->render.pango_layout,
                available_width * PANGO_SCALE);
        pango_layout_set_ellipsize(ctx->render.pango_layout, PANGO_ELLIPSIZE_MIDDLE);

        pango_layout_set_text(ctx->render.pango_layout, ctx->hypr.active_app, -1);
        int text_width = 0, text_height = 0;

        pango_layout_get_pixel_size(ctx->render.pango_layout,
                &text_width, &text_height);

        ctx->render.center_width = text_width + (2 * ctx->render.padding);

        cairo_set_source_rgb(ctx->render.cairo_t_shm, ctx->render.fg_color.r,
                ctx->render.fg_color.g, ctx->render.fg_color.b);

        cairo_move_to(ctx->render.cairo_t_shm,
                (ctx->wl.width - text_width) / 2,
                (ctx->wl.height - text_height) / 2);
        pango_cairo_show_layout(ctx->render.cairo_t_shm, ctx->render.pango_layout);

        pango_layout_set_width(ctx->render.pango_layout, -1);
        pango_layout_set_single_paragraph_mode(ctx->render.pango_layout, FALSE);
        pango_layout_set_ellipsize(ctx->render.pango_layout, PANGO_ELLIPSIZE_NONE);
    }
    else ctx->render.center_width = 2*ctx->render.padding;
}



// 3. RECHTS: System-Infos zeichnen
void draw_segment_right(struct app_context *ctx) {
    if (ctx->render.right_width) {
        segment_eraser(ctx,
                ctx->wl.width - ctx->render.right_width, 0,
                ctx->render.right_width, ctx->wl.height);
    }

    char right_bar_string[256];

    if (ctx->vitals.sys_time[0] == '\0') {
        get_iso_time(ctx->vitals.sys_time, sizeof(ctx->vitals.sys_time));
        get_ram_usage(&ctx->vitals, ctx->vitals.sys_ram, sizeof(ctx->vitals.sys_ram));
        ctx->vitals.sys_cpu = get_cpu_load(&ctx->vitals);
    }

    if (ctx->vitals.bat_available == 0) {
        get_battery_info(&ctx->vitals, ctx->vitals.sys_bat, sizeof(ctx->vitals.sys_bat));

        snprintf(right_bar_string, sizeof(right_bar_string),
                "CPU: %d%% | %s | %s ",
                 ctx->vitals.sys_cpu, ctx->vitals.sys_ram, ctx->vitals.sys_time);
    } else {
        snprintf(right_bar_string, sizeof(right_bar_string),
                "CPU: %d%% | %s | %s | %s ",
                 ctx->vitals.sys_cpu, ctx->vitals.sys_ram, ctx->vitals.sys_bat, ctx->vitals.sys_time);
    }

    pango_layout_set_text(ctx->render.pango_layout, right_bar_string, -1);

    int sys_width = 0, sys_height = 0;
    pango_layout_get_pixel_size(ctx->render.pango_layout, &sys_width, &sys_height);
    ctx->render.right_width = sys_width + ctx->render.padding;

    int right_x = ctx->wl.width - sys_width;

    cairo_set_source_rgb(ctx->render.cairo_t_shm, ctx->render.fg_color.r * 0.6,
            ctx->render.fg_color.g * 0.6, ctx->render.fg_color.b * 0.6);
    cairo_move_to(ctx->render.cairo_t_shm, right_x, (ctx->wl.height - sys_height) / 2);
    pango_cairo_show_layout(ctx->render.cairo_t_shm, ctx->render.pango_layout);
}



void draw_frame(struct app_context *ctx) {
    if (ctx->changed_segments == RENDER_ALL) {
        segment_eraser(ctx, 0, 0, ctx->wl.width, ctx->wl.height);
        wl_surface_damage_buffer(ctx->wl.surface,
                0, 0, ctx->wl.width, ctx->wl.height);
    }

    if (ctx->changed_segments & RENDER_LEFT) {
        int tmp_width = ctx->render.left_width;
        draw_segment_left(ctx);
        if (tmp_width < ctx->render.left_width) tmp_width = ctx->render.left_width;
        wl_surface_damage_buffer(ctx->wl.surface,
                0, 0, tmp_width, ctx->wl.height);
        ctx->changed_segments -= RENDER_LEFT;
    }

    if (ctx->changed_segments & RENDER_CENTER) {
        int tmp_width = ctx->render.center_width;
        draw_segment_center(ctx);
        if( tmp_width < ctx->render.center_width) tmp_width = ctx->render.center_width;
        wl_surface_damage_buffer(ctx->wl.surface,
                (ctx->wl.width - tmp_width)/2, 0,
                (ctx->wl.width/2) + (tmp_width/2), ctx->wl.height);
        ctx->changed_segments -= RENDER_CENTER;
    }

    if (ctx->changed_segments & RENDER_RIGHT) {
        int tmp_width = ctx->render.right_width;
        draw_segment_right(ctx);
        if( tmp_width < ctx->render.right_width) tmp_width = ctx->render.right_width;
        wl_surface_damage_buffer(ctx->wl.surface,
            ctx->wl.width-tmp_width, 0, tmp_width, ctx->wl.height);
        ctx->changed_segments -= RENDER_RIGHT;
    }

    wl_surface_attach(ctx->wl.surface, ctx->wl.buffer, 0, 0);
    wl_surface_commit(ctx->wl.surface);
}



struct color rgb_to_double(char *tmp) {
    unsigned int hex_val = 0;
    char hex_tmp[7] = {0};
    struct color ret;

    if (tmp[0] == '#') strncpy(hex_tmp, tmp+1, 6);
    else strncpy(hex_tmp, tmp, 6);

    if (sscanf(hex_tmp, "%x", &hex_val) == 1) {
        ret.r = ((hex_val >> 16) & 0xFF) / 255.0;
        ret.g = ((hex_val >> 8) & 0xFF) / 255.0;
        ret.b = (hex_val & 0xFF) / 255.0;
    }
    return ret;
}
