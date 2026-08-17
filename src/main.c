#include "basics.h"
#include "types.h"
#include "wayland-core.h"
#include "hyprland.h"
#include "window.h"
#include "buffer.h"
//#include "sys-vitals.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <ctype.h>



// forward declaration
static void config_fill_defaults(struct app_context *ctx);
static int setup_ctx(struct app_context *ctx);
static void cleanup(struct app_context *ctx);
static void handle_timeout(struct app_context *ctx);
static void handle_wayland_events(struct app_context *ctx, struct pollfd *fds);
static void handle_hyprland_events(struct app_context *ctx,struct pollfd *fd);
static void process_hyprland_event(struct app_context *ctx, const char *line);
static void setConfigValues(struct app_context *ctx);



// static pointer, only visible for aexit
static struct app_context *atexit_ctx_ptr = NULL;
// wrapper-function without param for atexit
void atexit_wrapper(void) {
    if (atexit_ctx_ptr) {
        cleanup(atexit_ctx_ptr);
    }
}



int main(int argc, char **argv) {
    struct app_context stack_ctx;
    struct pollfd fds[2];
    struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));

    atexit_ctx_ptr = ctx;
    if (atexit(atexit_wrapper) != 0) {
        fprintf(stderr, "Fehler: atexit konnte nicht registriert werden.\n");
        return 1;
    }

#ifndef DEBUG
    if (checkIfRunning()) return 0;
#endif
    zombieProtect();

    config_fill_defaults(ctx);
    if (configLoad(&ctx->config)) {
        setConfigValues(ctx);
        configFree(&ctx->config);
        printf("[wbar] falling back to default values\n");
    }
    if (setup_ctx(ctx)) return -1;
    if (optHandling(argc, argv, ctx)) return -1;


    if (create_sysvitals_fd(&ctx->vitals)) return -1;
    create_hyprland_socket(&ctx->hypr);
    initial_hyprland_query(&ctx->hypr);
    init_rendering(ctx);

    fds[0].fd = wl_display_get_fd(ctx->wl.display);
    fds[0].events = POLLIN;
    fds[1].fd = ctx->hypr.socket2_fd;
    fds[1].events = POLLIN;
#ifdef DEBUG
    printf("init done. starting main-loop\n");
    fflush(stdout);
#endif

// main program loop
    while (ctx->running) {
        // 1: Wayland-Handshake
        while (wl_display_prepare_read(ctx->wl.display) != 0) {
            if (wl_display_dispatch_pending(ctx->wl.display) < 0) {
                ctx->running = 0;
                break;
            }
        }

        if (wl_display_flush(ctx->wl.display) < 0) {
            wl_display_cancel_read(ctx->wl.display);
            break;
        }

        // Initial draw
        if (ctx->wl.configured && !ctx->initial_draw_done) {
            draw_frame(ctx);
            wl_display_flush(ctx->wl.display);
            ctx->initial_draw_done = 1;
        }

        // setting system vitals are read only every secound
        int ret = poll(fds, 2, DEF_SYS_VITAL_POLL_MS);
        if (ret < 0) {
            perror("poll error");
            wl_display_cancel_read(ctx->wl.display);
            break;
        }

        // 3: Handle results
        if (ret == 0) {
            wl_display_cancel_read(ctx->wl.display);
            // vitals are read only every secound
            handle_timeout(ctx);
        } else {
            // any other time wayland or hyprland is handled
            handle_wayland_events(ctx, fds);
            handle_hyprland_events(ctx, &fds[1]);
        }

        // 4: Error handling
        if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
            (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }
    }

    return 0;
}



static void handle_timeout(struct app_context *ctx) {
    get_iso_time(ctx->vitals.sys_time, sizeof(ctx->vitals.sys_time));
    get_ram_usage(&ctx->vitals, ctx->vitals.sys_ram, sizeof(ctx->vitals.sys_ram));
    get_battery_info(&ctx->vitals, ctx->vitals.sys_bat, sizeof(ctx->vitals.sys_bat));
    ctx->vitals.sys_cpu = get_cpu_load(&ctx->vitals);

    if (ctx->initial_draw_done) {
        ctx->changed_segments |= RENDER_RIGHT;
        draw_frame(ctx);
        wl_display_flush(ctx->wl.display);
    }
}



static void handle_wayland_events(struct app_context *ctx, struct pollfd *fds) {
    if (fds[0].revents & POLLIN) {
        if (wl_display_read_events(ctx->wl.display) < 0) {
            fprintf(stderr, "error at reading wayland events.\n");
            ctx->running = 0;
            return;
        }
        if (wl_display_dispatch_pending(ctx->wl.display) < 0) {
            ctx->running = 0;
            return;
        }
    }
    else wl_display_cancel_read(ctx->wl.display);
}



static void handle_hyprland_events(struct app_context *ctx,
        struct pollfd *hyprland_fd) {
    char ipc_buffer[16384];

    if (!(hyprland_fd->revents & POLLIN)) return;

    ssize_t len = recv(hyprland_fd->fd, ipc_buffer, sizeof(ipc_buffer) - 1, 0);
    if (len <= 0) {
        if (len == 0) fprintf(stderr, "hyprland closed the connection.\n");
        else perror("recv(hyprland)");
        ctx->running = 0;
        return;
    }

    ipc_buffer[len] = '\0';

    char *line_saveptr = NULL;
    char *line = strtok_r(ipc_buffer, "\n", &line_saveptr);

    while (line != NULL) {
        process_hyprland_event(ctx, line);
        line = strtok_r(NULL, "\n", &line_saveptr);
    }

    if (ctx->changed_segments && ctx->initial_draw_done) {
#ifdef DEBUG
        printf("actual frame Workspace=%s, App=%s\n",
               ctx->hypr.active_workspace, ctx->hypr.active_app);
        fflush(stdout);
#endif

        draw_frame(ctx);
        wl_display_flush(ctx->wl.display);
    }
}



static void process_hyprland_event(struct app_context *ctx, const char *line) {
    const char *delim = strstr(line, ">>");
    if (delim == NULL) return;

    const char *data = delim + 2;
    if (!data || *data == '\0') return;

    // A. Workspace change
    if (strstr(line, "workspace>>") == line) {
        strncpy(ctx->hypr.active_workspace, data, MAX_APP_NAME_LENGTH - 1);
        ctx->hypr.active_workspace[strlen(data) < MAX_APP_NAME_LENGTH - 1 ?
                strlen(data) : MAX_APP_NAME_LENGTH - 1] = '\0';
        ctx->changed_segments |= RENDER_LEFT;
        return;
    }

    // B. Active window change
    if (strstr(line, "activewindow>>") == line) {
        parse_hyprland_app_name(data, ctx->hypr.active_app, MAX_APP_NAME_LENGTH);
        ctx->changed_segments |= RENDER_CENTER;
        return;
    }

    // C. New window opened
    if (strstr(line, "openwindow>>") == line) {
        const char *ws_start = strchr(line, ',');
        if (ws_start) {
            int target_ws = atoi(ws_start + 1);
            int found = 0;

            for (int i = 0; i < ctx->hypr.workspaces_count; ++i) {
                if (ctx->hypr.workspaces[i].id == target_ws) {
                    ++ctx->hypr.workspaces[i].window_count;
                    found = 1;
                    ctx->changed_segments |= RENDER_LEFT + RENDER_CENTER;
                    return;
                }
            }
            if (!found && ctx->hypr.workspaces_count < MAX_WORKSPACES) {
                ctx->hypr.workspaces[ctx->hypr.workspaces_count].id = target_ws;
                ctx->hypr.workspaces[ctx->hypr.workspaces_count].window_count = 1;
                ++ctx->hypr.workspaces_count;
            }
            ctx->changed_segments |= RENDER_LEFT + RENDER_CENTER;
        }
    }

    // E. Hard refresh via JSON
    if (strstr(line, "destroyworkspace") == line ||
        strstr(line, "createworkspace") == line ||
        strstr(line, "closewindow>>") == line ||
        strstr(line, "movewindowv2>>") == line) {

        char *json_data = query_hyprland_ipc("workspaces");
        if (json_data) {
            char *ptr = json_data;
            ctx->hypr.workspaces_count = 0;
            while ((ptr = strstr(ptr, "{")) != NULL && ctx->hypr.workspaces_count < MAX_WORKSPACES) {
                char id_str[16] = {0};
                char win_str[16] = {0};
                get_json_value(ptr, "id", id_str, sizeof(id_str));
                get_json_value(ptr, "windows", win_str, sizeof(win_str));
                if (id_str[0] != '\0') {
                    ctx->hypr.workspaces[ctx->hypr.workspaces_count].id = atoi(id_str);
                    ctx->hypr.workspaces[ctx->hypr.workspaces_count].window_count = atoi(win_str);
                    ctx->hypr.workspaces_count++;
                }
                ++ptr;
            }
            free(json_data);
            ctx->changed_segments |= RENDER_LEFT + RENDER_CENTER;
        }
    }
}



static void setConfigValues(struct app_context *ctx) {
    ctx->render.padding = atoi(configGetValueFromName(&ctx->config, "padding"));
    strncpy(ctx->render.font, configGetValueFromName(&ctx->config, "font"),MAX_APP_NAME_LENGTH);
    ctx->render.bg_color =  rgb_to_double(configGetValueFromName(&ctx->config, "bg_color"));
    ctx->render.fg_color =  rgb_to_double(configGetValueFromName(&ctx->config, "fg_color"));
    ctx->render.accent_color =  rgb_to_double(configGetValueFromName(&ctx->config, "ac_color"));
    ctx->wl.height = atoi(configGetValueFromName(&ctx->config, "bar_height"));
}



static void config_fill_defaults(struct app_context *ctx) {
    ctx->render.bg_color.r = DEF_BG_COL_R;
    ctx->render.bg_color.g = DEF_BG_COL_G;
    ctx->render.bg_color.b = DEF_BG_COL_B;
    ctx->render.accent_color.r = DEF_ACC_COL_R;
    ctx->render.accent_color.g = DEF_ACC_COL_G;
    ctx->render.accent_color.b = DEF_ACC_COL_B;
    ctx->render.fg_color.r = DEF_FG_COL_R;
    ctx->render.fg_color.g = DEF_FG_COL_G;
    ctx->render.fg_color.b = DEF_FG_COL_B;
    ctx->render.padding = DEF_PADDING;
    ctx->wl.height = DEF_BAR_HEIGHT;

    ctx->render.font = calloc(MAX_APP_NAME_LENGTH, sizeof(char));
    if (ctx->render.font) strcpy(ctx->render.font, DEF_FONT);
}



static int setup_ctx(struct app_context *ctx) {
    ctx->running = 1;
    ctx->wl.width = 0;
    ctx->wl.configured = 0;
    ctx->initial_draw_done = 0;
    ctx->hypr.active_workspace = calloc(MAX_APP_NAME_LENGTH, sizeof(char));
    ctx->hypr.active_app = calloc(MAX_APP_NAME_LENGTH, sizeof(char));
    ctx->changed_segments = RENDER_ALL;
    ctx->render.left_width = 0;
    ctx->render.center_width = 0;
    ctx->render.right_width = 0;
    ctx->wl.display = wl_display_connect(NULL);

    if (!ctx->wl.display) return 1;

    ctx->wl.registry = wl_display_get_registry(ctx->wl.display);
    wl_registry_add_listener(ctx->wl.registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->wl.display);

    if (ctx->wl.seat) wl_seat_add_listener(ctx->wl.seat, &seat_listener, ctx);

    wl_display_roundtrip(ctx->wl.display);

    if (!ctx->wl.compositor || !ctx->wl.layer_shell || !ctx->wl.shm) {
        fprintf(stderr, "Err: critical Wayland handler missing.\n");
        wl_display_disconnect(ctx->wl.display);
        return 1;
    }

    ctx->wl.surface = wl_compositor_create_surface(ctx->wl.compositor);
    ctx->wl.layer_surface = zwlr_layer_shell_v1_get_layer_surface(ctx->wl.layer_shell,
            ctx->wl.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP,"wbar");

    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(ctx->wl.layer_surface, anchors);
    zwlr_layer_surface_v1_set_size(ctx->wl.layer_surface, 0, ctx->wl.height);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->wl.layer_surface, ctx->wl.height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->wl.layer_surface, 0);
    zwlr_layer_surface_v1_add_listener(ctx->wl.layer_surface,
            &layer_surface_listener, ctx);

    wl_surface_commit(ctx->wl.surface);
    wl_display_flush(ctx->wl.display);

    return 0;
}



static void cleanup(struct app_context *ctx) {
    if (ctx->wl.surface) {
        wl_surface_attach(ctx->wl.surface, NULL, 0, 0);
        wl_surface_commit(ctx->wl.surface);
    }

    wl_display_flush(ctx->wl.display);

    if (ctx->wl.display) wl_display_roundtrip(ctx->wl.display);
    if (ctx->wl.seat) wl_seat_destroy(ctx->wl.seat);
    if (ctx->wl.layer_surface) zwlr_layer_surface_v1_destroy(ctx->wl.layer_surface);
    if (ctx->wl.surface) wl_surface_destroy(ctx->wl.surface);
    if (ctx->wl.buffer) wl_buffer_destroy(ctx->wl.buffer);
    if (ctx->wl.display) wl_display_disconnect(ctx->wl.display);
    if (ctx->hypr.active_workspace) free(ctx->hypr.active_workspace);
    free(ctx->hypr.active_app);
    if (ctx->render.font) free(ctx->render.font);
    close(ctx->hypr.socket2_fd);
    cleanup_sysvitals(&ctx->vitals);
    cleanup_rendering(ctx);
}
