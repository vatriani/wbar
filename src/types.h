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

    double                         bg_r, bg_g, bg_b;
    double                         accent_r, accent_g, accent_b;

    int                            running;
    int                            width;
    int                            height;
    int                            configured;
};

#endif
