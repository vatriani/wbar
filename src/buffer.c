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



static const int padding = 5;



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
    (void)data; (void)wl_buffer;
}



static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};



int init_rendering(struct app_context *ctx) {
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, ctx->width);
    ctx->shm_size = stride * ctx->height;

    ctx->shm_fd = allocate_shm_file(ctx->shm_size);
    if (ctx->shm_fd < 0) return -1;

    ctx->shm_data = mmap(NULL, ctx->shm_size, PROT_READ | PROT_WRITE,
        MAP_SHARED, ctx->shm_fd, 0);
    if (ctx->shm_data == MAP_FAILED) {
        close(ctx->shm_fd);
        ctx->shm_fd = -1;
        return -1;
    }
    memset(ctx->shm_data, 0, ctx->shm_size);

    ctx->cairo_surface_shm = cairo_image_surface_create_for_data(
        (unsigned char *)ctx->shm_data, CAIRO_FORMAT_RGB24,
        ctx->width, ctx->height, stride);
    ctx->cairo_t_shm = cairo_create(ctx->cairo_surface_shm);

    ctx->pango_layout = pango_cairo_create_layout(ctx->cairo_t_shm);
    ctx->pango_font_desc = pango_font_description_from_string(ctx->font);
    pango_layout_set_font_description(ctx->pango_layout, ctx->pango_font_desc);

    ctx->wl_pool = wl_shm_create_pool(ctx->shm, ctx->shm_fd, ctx->shm_size);
    ctx->wl_buffer = wl_shm_pool_create_buffer(ctx->wl_pool,
            0, ctx->width, ctx->height, stride, WL_SHM_FORMAT_XRGB8888);

    if (!ctx->wl_buffer) {
        wl_shm_pool_destroy(ctx->wl_pool);
        munmap(ctx->shm_data, ctx->shm_size);
        close(ctx->shm_fd);
        return -1;
    }

    wl_buffer_add_listener(ctx->wl_buffer, &buffer_listener, ctx);
    ctx->changed_segments = RENDER_ALL;

    return 0;
}



void cleanup_rendering(struct app_context *ctx) {
    if (ctx->pango_font_desc) pango_font_description_free(ctx->pango_font_desc);
    if (ctx->pango_layout) g_object_unref(ctx->pango_layout);
    if (ctx->wl_buffer) wl_buffer_destroy(ctx->wl_buffer);
    if (ctx->shm_data) munmap(ctx->shm_data, ctx->width * 4 * ctx->height);
    if (ctx->shm_fd >= 0) close(ctx->shm_fd);
    cairo_destroy(ctx->cairo_t_shm);
    cairo_surface_destroy(ctx->cairo_surface_shm);
}



// 1. LINKS: Workspaces zeichnen
void draw_segment_left(struct app_context *ctx) {
    if (ctx->left_width) {
        cairo_rectangle(ctx->cairo_t_shm, 0, 0, ctx->left_width, ctx->height);
        cairo_set_source_rgb(ctx->cairo_t_shm, ctx->bg_color.r,
            ctx->bg_color.g, ctx->bg_color.b);
        cairo_fill(ctx->cairo_t_shm);
    }

    // 2. DEIN ORIGINALER WORKSPACE SORTIER-CODE (Nur 1x ausführen!)
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

    // 3. DER EINZIGE ZEICHEN-DURCHLAUF (Direkt messen und malen)
    int current_x = padding;
    int text_width = 0, text_height = 0;

    for (int i = 0; i < ctx->workspace_count; ++i) {
        char item[128];
        char id_str[32];
        snprintf(id_str, sizeof(id_str), "%d", sorted_ids[i]);

        if (sorted_windows[i] > 0) {
            snprintf(item, sizeof(item), " [%d] - %d ", sorted_ids[i],
                    sorted_windows[i]);
        } else {
            snprintf(item, sizeof(item), " [%d] ", sorted_ids[i]);
        }

        pango_layout_set_text(ctx->pango_layout, item, -1);
        pango_layout_get_pixel_size(ctx->pango_layout, &text_width,
            &text_height);


        if (ctx->active_workspace && strcmp(ctx->active_workspace, id_str)
                == 0) {
            cairo_set_source_rgb(ctx->cairo_t_shm, ctx->accent_color.r,
                    ctx->accent_color.g, ctx->accent_color.b);
            cairo_rectangle(ctx->cairo_t_shm, current_x, 0, text_width,
                    ctx->height);
            cairo_fill(ctx->cairo_t_shm);
        }

        cairo_set_source_rgb(ctx->cairo_t_shm, ctx->fg_color.r,
                ctx->fg_color.g, ctx->fg_color.b);
        cairo_move_to(ctx->cairo_t_shm, current_x,
                (ctx->height - text_height) / 2);
        pango_cairo_show_layout(ctx->cairo_t_shm, ctx->pango_layout);
        current_x += text_width;

        if (i < ctx->workspace_count - 1) {
            pango_layout_set_text(ctx->pango_layout, " | ", -1);
            pango_layout_get_pixel_size(ctx->pango_layout, &text_width,
                    &text_height);

            cairo_set_source_rgb(ctx->cairo_t_shm, ctx->fg_color.r * 0.6,
                    ctx->fg_color.g * 0.6, ctx->fg_color.b * 0.6);
            cairo_move_to(ctx->cairo_t_shm, current_x,
                    (ctx->height - text_height) / 2);
            pango_cairo_show_layout(ctx->cairo_t_shm, ctx->pango_layout);
            current_x += text_width;
        }
    }

    ctx->left_width = current_x + padding;
}



// 2. MITTE: Aktive App zeichnen
void draw_segment_center(struct app_context *ctx) {
    if (ctx->center_width) {
        cairo_rectangle(ctx->cairo_t_shm,
                (ctx->width - ctx->center_width) / 2,
                0,
                ctx->center_width,
                ctx->height);
        cairo_set_source_rgb(ctx->cairo_t_shm, ctx->bg_color.r,
                ctx->bg_color.g, ctx->bg_color.b);
        cairo_fill(ctx->cairo_t_shm);
    }

     if (ctx->active_app && ctx->active_app[0] != '\0') {
        int available_width = ctx->width - ctx->left_width -
                ctx->right_width - (4 * padding);
        pango_layout_set_single_paragraph_mode(ctx->pango_layout, TRUE);
        pango_layout_set_width(ctx->pango_layout,
                available_width * PANGO_SCALE);
        pango_layout_set_ellipsize(ctx->pango_layout, PANGO_ELLIPSIZE_MIDDLE);

        pango_layout_set_text(ctx->pango_layout, ctx->active_app, -1);
        int text_width = 0, text_height = 0;

        pango_layout_get_pixel_size(ctx->pango_layout,
                &text_width, &text_height);

        ctx->center_width = text_width + (2 * padding);

        cairo_set_source_rgb(ctx->cairo_t_shm, ctx->fg_color.r,
                ctx->fg_color.g, ctx->fg_color.b);

        cairo_move_to(ctx->cairo_t_shm,
                (ctx->width - text_width) / 2,
                (ctx->height - text_height) / 2);
        pango_cairo_show_layout(ctx->cairo_t_shm, ctx->pango_layout);

        pango_layout_set_width(ctx->pango_layout, -1);
        pango_layout_set_single_paragraph_mode(ctx->pango_layout, FALSE);
        pango_layout_set_ellipsize(ctx->pango_layout, PANGO_ELLIPSIZE_NONE);
    }
    else ctx->center_width = 2*padding;
}



// 3. RECHTS: System-Infos zeichnen
void draw_segment_right(struct app_context *ctx) {
    if (ctx->right_width) {
        cairo_rectangle(ctx->cairo_t_shm,
                ctx->width - ctx->right_width,
                0,
                ctx->right_width,
                ctx->height);
        cairo_set_source_rgb(ctx->cairo_t_shm, ctx->bg_color.r,
                ctx->bg_color.g, ctx->bg_color.b);
        cairo_fill(ctx->cairo_t_shm);
    }

    if (ctx->sys_time[0] == '\0') {
        get_iso_time(ctx->sys_time, sizeof(ctx->sys_time));
        get_ram_usage(ctx->sys_ram, sizeof(ctx->sys_ram));
        get_battery_info(ctx->sys_bat, sizeof(ctx->sys_bat));
        ctx->sys_cpu = get_cpu_load();
    }

    char right_bar_string[256];
    if (strstr(ctx->sys_bat, "BAT N/A") != NULL) {
        snprintf(right_bar_string, sizeof(right_bar_string),
                "CPU: %d%% | %s | %s ",
                 ctx->sys_cpu, ctx->sys_ram, ctx->sys_time);
    } else {
        snprintf(right_bar_string, sizeof(right_bar_string),
                "CPU: %d%% | %s | %s | %s ",
                 ctx->sys_cpu, ctx->sys_ram, ctx->sys_bat, ctx->sys_time);
    }

    pango_layout_set_text(ctx->pango_layout, right_bar_string, -1);

    int sys_width = 0, sys_height = 0;
    pango_layout_get_pixel_size(ctx->pango_layout, &sys_width, &sys_height);
    ctx->right_width = sys_width + padding;

    int right_x = ctx->width - sys_width;

    cairo_set_source_rgb(ctx->cairo_t_shm, ctx->fg_color.r * 0.6,
            ctx->fg_color.g * 0.6, ctx->fg_color.b * 0.6);
    cairo_move_to(ctx->cairo_t_shm, right_x, (ctx->height - sys_height) / 2);
    pango_cairo_show_layout(ctx->cairo_t_shm, ctx->pango_layout);
}



void draw_frame(struct app_context *ctx) {
    // --- PARTIAL RENDERING ---
    if (ctx->changed_segments == RENDER_ALL) {
        cairo_rectangle(ctx->cairo_t_shm,
            0,
            0,
            ctx->width,
            ctx->height);
        cairo_set_source_rgb(ctx->cairo_t_shm, ctx->bg_color.r,
                ctx->bg_color.g, ctx->bg_color.b);
        cairo_fill(ctx->cairo_t_shm);
        wl_surface_damage_buffer(ctx->surface,
                0,
                0,
                ctx->width,
                ctx->height);
    }

    if (ctx->changed_segments & RENDER_LEFT) {
        int tmp_width = ctx->left_width;
        draw_segment_left(ctx);
        if (tmp_width < ctx->left_width) tmp_width = ctx->left_width;
        wl_surface_damage_buffer(ctx->surface,
                0,
                0,
                tmp_width,
                ctx->height);
        ctx->changed_segments -= RENDER_LEFT;
    }

    if (ctx->changed_segments & RENDER_CENTER) {
        int tmp_width = ctx->center_width;
        draw_segment_center(ctx);
        if( tmp_width < ctx->center_width) tmp_width = ctx->center_width;
        wl_surface_damage_buffer(ctx->surface,
                (ctx->width - tmp_width)/2 ,
                0,
                (ctx->width/2) + (tmp_width/2),
                ctx->height);
        ctx->changed_segments -= RENDER_CENTER;
    }

    if (ctx->changed_segments & RENDER_RIGHT) {
        int tmp_width = ctx->right_width;
        draw_segment_right(ctx);
        if( tmp_width < ctx->right_width) tmp_width = ctx->right_width;
        wl_surface_damage_buffer(ctx->surface,
            ctx->width-tmp_width,
            0,
            tmp_width,
            ctx->height);
        ctx->changed_segments -= RENDER_RIGHT;
    }

    wl_surface_attach(ctx->surface, ctx->wl_buffer, 0, 0);
    wl_surface_commit(ctx->surface);
}
