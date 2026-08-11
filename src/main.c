#define _GNU_SOURCE

#include "basics.h"
#include "types.h"
#include "wayland-core.h"
#include "hyprland.h"
#include "window.h"
#include "buffer.h"
#include "sys-vitals.h"
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
uint32_t setup_ctx(struct app_context *ctx);
static void cleanup(struct app_context *ctx);



int main(int argc, char **argv) {
    int hyprland_sock = 0;
    struct app_context stack_ctx;
    struct pollfd fds[2];
    static char ipc_buffer[16384];
    int initial_draw_done = 0;
    struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));

    if (checkIfRunning()) return 0;
    zombieProtect();
    if (setup_ctx(ctx)) return 0;
    fetch_hyprland_colors(ctx);
    if (optHandling(argc, argv, ctx)) return 0;
    create_hyprland_socket(&hyprland_sock);
    initial_hyprland_query(ctx);

    fds[0].fd = wl_display_get_fd(ctx->display);
    fds[0].events = POLLIN;
    fds[1].fd = hyprland_sock;
    fds[1].events = POLLIN;
#ifdef DEBUG
    printf("init done. starting main-loop\n");
#endif
    fflush(stdout);

// main program loop
    while (ctx->running) {
        if (!ctx->running) break;
        // 1: Wayland-Handshake
        while (wl_display_prepare_read(ctx->display) != 0) {
            if (wl_display_dispatch_pending(ctx->display) < 0) {
                ctx->running = 0;
                break;
            }
        }

        if (wl_display_flush(ctx->display) < 0) {
            wl_display_cancel_read(ctx->display);
            break;
        }

        if (ctx->configured && !initial_draw_done && ctx->width > 0) {
            draw_frame(ctx);
            initial_draw_done = 1;
            wl_display_flush(ctx->display);
        }

        // 2: block waiting (Poll)
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            perror("poll error");
            wl_display_cancel_read(ctx->display);
            break;
        }
        if (ret == 0) {
            wl_display_cancel_read(ctx->display);
            get_iso_time(ctx->sys_time, sizeof(ctx->sys_time));
            get_ram_usage(ctx->sys_ram, sizeof(ctx->sys_ram));
            get_battery_info(ctx->sys_bat, sizeof(ctx->sys_bat));
            ctx->sys_cpu = get_cpu_load();
            if (initial_draw_done) draw_frame(ctx);
            continue; // Direkt zum nächsten Schleifendurchlauf springen
        }

        // 3: wayland event Handling
        if (fds[0].revents & POLLIN) {
            if (wl_display_read_events(ctx->display) < 0) {
                fprintf(stderr, "error at reading wayland events.\n");
                break;
            }
            if (wl_display_dispatch_pending(ctx->display) < 0) break;
        } else
            wl_display_cancel_read(ctx->display);

        // 4: dynamic desktop-tracking
        if (fds[1].revents & POLLIN) {
            ssize_t len = recv(hyprland_sock, ipc_buffer, sizeof(ipc_buffer) - 1, 0);
            if (len <= 0) {
                fprintf(stderr, "loosing connection to hyprland.\n");
                break;
            }

            ipc_buffer[len] = '\0';
            int state_changed = 0;

            char *line_saveptr = NULL;
            char *line = strtok_r(ipc_buffer, "\n", &line_saveptr);

            while (line != NULL) {
                char *delim = strstr(line, ">>");

                if (delim != NULL) {
                    char *data = delim + 2;

                    if (data && *data != '\0') {
                        // changing workspace
                        if (strstr(line, "workspace>>") == line) {
                            strncpy(ctx->active_workspace, data, 1023);
                            ctx->active_workspace[strlen(data) < 1023 ? strlen(data) : 1023] = '\0';
                            state_changed = 1;
                        }
                        // changing active app window
                        else if (strstr(line, "activewindow>>") == line) {
                            strncpy(ctx->active_app, data, 1023);
                            ctx->active_app[strlen(data) < 1023 ? strlen(data) : 1023] = '\0';
                            state_changed = 1;
                        }
                        // open app window event
                        else if (strstr(line, "openwindow>>") == line) {
                            char *ws_start = strchr(line, ',');
                            if (ws_start) {
                                int target_ws = atoi(ws_start + 1);
                                int found = 0;

                                for (unsigned short int i = 0; i < ctx->workspace_count; ++i) {
                                    if (ctx->workspaces[i] == target_ws) {
                                        ++ctx->workspace_windows[i];
                                        found = 1;
                                        state_changed = 1;
                                        break;
                                    }
                                }
                                if (!found && ctx->workspace_count < 32) {
                                    ctx->workspaces[ctx->workspace_count] = target_ws;
                                    ctx->workspace_windows[ctx->workspace_count] = 1;
                                    ++ctx->workspace_count;
                                    state_changed = 1;
                                }
                            }
                        }
                        // movetoworkspace event
                        else if (strstr(line, "movewindowv2>>") == line) {
                            char *ws_start = strchr(line, ',');
                            if (ws_start) {
                                int target_ws = atoi(ws_start + 1);
                                int found = 0;
                                for (unsigned short int i = 0; i < ctx->workspace_count; ++i) {
                                    if (ctx->workspaces[i] == target_ws) {
                                        ++ctx->workspace_windows[i];
                                        found = 1;
                                        break;
                                    }
                                }
                                if (!found && ctx->workspace_count < 32) {
                                    ctx->workspaces[ctx->workspace_count] = target_ws;
                                    ctx->workspace_windows[ctx->workspace_count] = 1;
                                    ctx->workspace_count++;
                                }
                                state_changed = 1;
                            }
                        }
                        // E. Hard-Refresh via JSON
                        else if (strstr(line, "closewindow>>") == line ||
                                 strstr(line, "destroyworkspace") == line ||
                                  strstr(line, "createworkspace") == line) {
                            char *uj = query_hyprland_ipc("workspaces");

                            if (uj) {
                                char *p = uj;
                                int c = 0;

                                while ((p = strstr(p, "\"id\":")) != NULL && c < 32) {
                                    int id = 0;
                                    int win = 0;

                                    if (sscanf(p, "\"id\": %d", &id) == 1) {
                                        ctx->workspaces[c] = id;
                                        char *w_p = strstr(p, "\"windows\":");
                                        if (w_p) sscanf(w_p, "\"windows\": %d", &win);
                                        ctx->workspace_windows[c] = win; ++c;
                                    }
                                    p += 5;
                                }
                                ctx->workspace_count = c;
                                free(uj);
                                state_changed = 1;
                            }
                        }
                    }
                }
                line = strtok_r(NULL, "\n", &line_saveptr);
            }

            // triggers rendering when something changing or in initial draw
            if (state_changed && initial_draw_done) {
#ifdef DEBUG
                printf("actual frame Workspace=%s, App=%s\n",
                        ctx->active_workspace, ctx->active_app);
#endif
                fflush(stdout);
                draw_frame(ctx);
            }
        }

        if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
            (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }
    }

    close(hyprland_sock);
    cleanup(ctx);

    return 0;
}



uint32_t setup_ctx(struct app_context *ctx) {
    ctx->running = 1;
    ctx->width = 0;
    ctx->height = 16;
    ctx->configured = 0;
    ctx->active_workspace = calloc(1024, sizeof(char));
    ctx->active_app = calloc(1024, sizeof(char));
    ctx->font = calloc(1024, sizeof(char));

    if (ctx->font) strcpy((char *)ctx->font, "DejaVu Sans 12");
    ctx->display = wl_display_connect(NULL);

    if (!ctx->display) return 1;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    if (ctx->seat) wl_seat_add_listener(ctx->seat, &seat_listener, ctx);

    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->layer_shell || !ctx->shm) {
        fprintf(stderr, "Err: critical Wayland handler missing.\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }

    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(ctx->layer_shell,
            ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP,"wbar");

    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, anchors);
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, ctx->height);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, ctx->height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, 0);
    zwlr_layer_surface_v1_add_listener(ctx->layer_surface,
            &layer_surface_listener, ctx);

    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);

    return 0;
}



static void cleanup(struct app_context *ctx) {
    if (ctx->surface) {
        wl_surface_attach(ctx->surface, NULL, 0, 0);
        wl_surface_commit(ctx->surface);
    }

    wl_display_flush(ctx->display);

    if (ctx->display)       wl_display_roundtrip(ctx->display);
    if (ctx->seat)          wl_seat_destroy(ctx->seat);
    if (ctx->layer_surface) zwlr_layer_surface_v1_destroy(ctx->layer_surface);
    if (ctx->surface)       wl_surface_destroy(ctx->surface);
    if (ctx->buffer)        wl_buffer_destroy(ctx->buffer);
    if (ctx->display)       wl_display_disconnect(ctx->display);

    if (ctx->active_workspace) free(ctx->active_workspace);
    if (ctx->active_app)       free(ctx->active_app);
    if (ctx->font)             free(ctx->font);
}
