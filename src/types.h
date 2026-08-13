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
#include <cairo.h>
#include <pango/pangocairo.h>



#define MAX_WORKSPACES    32
#define MAX_BUFF_SYS      64
#define MAX_APP_NAME_LENGTH    1024



struct color {
    double r;
    double g;
    double b;
};



struct workspace {
    int id;
    int window_count;
};



struct sys_vitals {
    char sys_time[MAX_BUFF_SYS];
    char sys_ram[MAX_BUFF_SYS];
    char sys_bat[MAX_BUFF_SYS];
    int  sys_cpu;
};



struct wayland_context {
    struct wl_compositor *compositor;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct wl_surface *surface;
    struct zwlr_layer_shell_v1    *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_shm_pool *pool;
    struct wl_buffer *buffer;
    int configured;
    int width;
    int height;
};



struct render_context {
    void *shm_data;
    size_t shm_size;
    int shm_fd;
    cairo_surface_t *cairo_surface_shm;
    cairo_t *cairo_t_shm;
    PangoLayout *pango_layout;
    PangoFontDescription *pango_font_desc;
    struct color bg_color;
    struct color fg_color;
    struct color accent_color;
    int padding;
    char *font;
    int left_width;
    int center_width;
    int right_width;
};



struct hyprland_context {
    int socket_fd;
    char *active_workspace;
    char *active_app;
    struct workspace workspaces[MAX_WORKSPACES];
    int workspaces_count;
};



typedef enum {
    RENDER_LEFT   = (1 << 0),
    RENDER_CENTER = (1 << 1),
    RENDER_RIGHT  = (1 << 2),
    RENDER_ALL    = 0xFF
} render_segments_t;



struct app_context {
    volatile unsigned short int running;
    unsigned short int initial_draw_done;
    uint32_t changed_segments;

    struct wayland_context  wl;
    struct render_context   render;
    struct hyprland_context hypr;
    struct sys_vitals       vitals;
};

#endif
