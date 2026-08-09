#ifndef TYPES_H
#define TYPES_H

#define _GNU_SOURCE



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

    int                            click_x;
    int                            click_y;
    int                            workspace_positions[32];
    int                            workspace_widths[32];
    int                            mouse_over_bar;
    int                            pending_workspace_change;

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
    double                         bg_r, bg_g, bg_b;
    double                         accent_r, accent_g, accent_b;
    double                         fg_r, fg_g, fg_b;

    int                            running;
    int                            width;
    int                            height;
    int                            configured;
};

#endif
