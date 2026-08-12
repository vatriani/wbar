/**
 *  @file types.h
 *  @brief Defines our context struct.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */
#ifndef TYPES_H
#define TYPES_H

#define _GNU_SOURCE

#include <stdint.h>
#include <pango/pangocairo.h>



struct color {
    double r;
    double g;
    double b;
};



typedef enum {
    RENDER_LEFT   = (1 << 0),
    RENDER_CENTER = (1 << 1),
    RENDER_RIGHT  = (1 << 2),
    RENDER_ALL    = 0xFF
} render_segments_t;



struct app_context {
    struct wl_display             *display;
    struct wl_registry            *registry;
    struct wl_compositor          *compositor;
    struct zwlr_layer_shell_v1    *layer_shell;
    struct wl_shm                 *shm;
    struct wl_seat                *seat;
    struct wl_keyboard            *keyboard;
    struct wl_surface             *surface;
    struct zwlr_layer_surface_v1  *layer_surface;
    struct wl_buffer              *buffer;
    struct wl_pointer             *pointer;
    struct wl_shm_pool            *wl_pool;

    int                            workspaces[32];
    int                            workspace_windows[32];
    int                            workspace_count;
    char                          *active_workspace;
    char                          *active_app;

    char                           sys_time[64];
    char                           sys_ram[32];
    char                           sys_bat[32];
    int                            sys_cpu;

    char                          *font;
    struct color                   bg_color;
    struct color                   fg_color;
    struct color                   accent_color;

    int                            running;
    int                            width;
    int                            height;
    int                            configured;
    int                            initial_draw_done;

// some pango cairo management
    uint32_t                      *shm_data;
    size_t                         shm_size;
    int                            shm_fd;
    struct wl_buffer              *wl_buffer;
    int                            changed_segments;
    int                            left_width;
    int                            center_width;
    int                            right_width;

    cairo_surface_t               *cairo_surface_shm;
    cairo_t                       *cairo_t_shm;
    PangoLayout                   *pango_layout;
    PangoFontDescription          *pango_font_desc;
};

#endif
