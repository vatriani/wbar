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

#include "basics-t.h"

#include <stdint.h>
#include <cairo.h>
#include <pango/pangocairo.h>



#define MAX_WORKSPACES             32
#define MAX_BUFF_SYS               32
#define MAX_APP_NAME_LENGTH      1024
#define APP_NAME               "wbar"
#define CONFIG_FILE_NAME "config.cfg"

#define DEF_BG_COL_R         0.117
#define DEF_BG_COL_G         0.117
#define DEF_BG_COL_B         0.180
#define DEF_ACC_COL_R        0.321
#define DEF_ACC_COL_G        0.443
#define DEF_ACC_COL_B        0.654
#define DEF_FG_COL_R           0.9
#define DEF_FG_COL_G           0.9
#define DEF_FG_COL_B           0.9
#define DEF_PADDING              5
#define DEF_BAR_HEIGHT          16
#define DEF_FONT  "DejaVu Sans 12"
#define DEF_SYS_VITAL_POLL_MS 1000



typedef enum {
    RENDER_LEFT   = (1 << 0),
    RENDER_CENTER = (1 << 1),
    RENDER_RIGHT  = (1 << 2),
    RENDER_ALL    = 0xFF
} render_segments_t;



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
    struct wl_compositor         *compositor;
    struct wl_display            *display;
    struct wl_registry           *registry;
    struct wl_seat               *seat;
    struct wl_shm                *shm;
    struct wl_surface            *surface;
    struct zwlr_layer_shell_v1   *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_shm_pool           *pool;
    struct wl_buffer             *buffer;

    int                           configured;
    int                           width;
    int                           height;
};



struct render_context {
    void                 *shm_data;
    size_t                shm_size;
    int                   shm_fd;

    cairo_surface_t      *cairo_surface_shm;
    cairo_t              *cairo_t_shm;
    PangoLayout          *pango_layout;
    PangoFontDescription *pango_font_desc;

    struct color          bg_color;
    struct color          fg_color;
    struct color          accent_color;
    int                   padding;
    char                 *font;
    int                   left_width;
    int                   center_width;
    int                   right_width;
};



struct hyprland_context {
    int               socket_fd;
    char             *active_workspace;
    char             *active_app;
    struct workspace  workspaces[MAX_WORKSPACES];
    int               workspaces_count;
};



struct app_context {
    volatile unsigned short int running;
    unsigned short int initial_draw_done;
    uint32_t changed_segments;

    struct wayland_context  wl;
    struct render_context   render;
    struct hyprland_context hypr;
    struct sys_vitals       vitals;
    config_file             config;
};

#endif
