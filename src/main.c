#define _GNU_SOURCE

#include "basics.h"
#include "types.h"
#include "wayland-core.h"
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



static uint32_t setup(struct app_context *ctx) {
    ctx->running = 1;
    ctx->width = 0;
    ctx->height = 16;
    ctx->configured = 0;
    ctx->pending_workspace_change = 0;
    ctx->active_workspace = calloc(1024, sizeof(char));
    ctx->active_app = calloc(1024, sizeof(char));
    ctx->font = calloc(1024, sizeof(char));
    if (ctx->font) {
        strcpy((char *)ctx->font, "DejaVu Sans 12");
    }

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) return 1;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    // FIX 3: Erst JETZT, wo der Seat garantiert gebunden ist, den Listener anhängen!
    if (ctx->seat) {
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    }

    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->layer_shell || !ctx->shm) {
        fprintf(stderr, "Err: critical Wayland handler missing.\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }

    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "wbar"
    );

    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, anchors);

    // WICHTIG: Die Größe initial auf 0 (Breite) und 16 (Höhe) setzen
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, ctx->height);

    // FIX 1: Die exklusive Zone muss exakt der Höhe (16) entsprechen!
    // Das signalisiert Hyprland, dass dieses Fenster ein Panel ist und Klicks trotz Stufe 0 durchlässt.
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, ctx->height);

    // FIX 2: Interaktivität zwingend auf 0 (False) setzen!
    // Dadurch wird die Maus NIEMALS gefangen. Klicks funktionieren über den Pointer trotzdem,
    // weil Hyprland die exklusive Zone oben als Panel-Klickbereich freigibt.
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, 0);

    zwlr_layer_surface_v1_add_listener(ctx->layer_surface, &layer_surface_listener, ctx);

    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);
    return 0;
}



static void destroy(struct app_context *ctx) {
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

    free(ctx->active_workspace);
    free(ctx->active_app);
    free(ctx->font);
}



int main(int argc, char **argv) {
    struct app_context stack_ctx;
    register struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));

    if (checkIfRunning()) return 0;
    zombieProtect();

    if(setup(ctx)) return 0;

    fetch_hyprland_colors(ctx);

    char *active_reply = query_hyprland_ipc("activeworkspace");
    if (active_reply) {
        // Da die Antwort im JSON-Format kommt (z.B. {"id":2,"name":"2"}),
        // suchen wir intelligent nach dem Schlüssel "id": oder "name":"
        char *id_pos = strstr(active_reply, "\"id\":");
        if (id_pos) {
            id_pos += 5; // Springe hinter das '"id":'
            // Falls dort ein Leerzeichen ist, überspringen
            while (*id_pos == ' ') id_pos++;

            // Die gefundene ID-Ziffer in deinen active_workspace-String kopieren
            int idx = 0;
            while (isdigit((unsigned char)id_pos[idx]) && idx < 10) {
                ctx->active_workspace[idx] = id_pos[idx];
                idx++;
            }
            ctx->active_workspace[idx] = '\0';
        } else {
            // Fallback: Falls die API flach antwortet (reine Zahl), direkt kopieren
            strncpy(ctx->active_workspace, active_reply, 10);
            // Eventuelle Newlines am Ende abschneiden
            ctx->active_workspace[strcspn(ctx->active_workspace, "\r\n")] = '\0';
        }

        free(active_reply); // IPC-Puffer sauber freigeben
    }

    // Falls die Abfrage fehlschlug, setzen wir eine sichere "1" als Fallback
    if (ctx->active_workspace[0] == '\0') {
        strcpy(ctx->active_workspace, "1");
    }

    if (!optHandling(argc, argv, ctx)) return 0;

    char socket_path[512] = {0};

    // Warte-Schleife für den Pfad
    while (!get_socket_path(socket_path, sizeof(socket_path))) sleep(1);

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return 1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Verbindung zum Hyprland Event-Socket herstellen
    while (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) sleep(1);

    struct pollfd fds[2];
    fds[0].fd = wl_display_get_fd(ctx->display);
    fds[0].events = POLLIN;
    fds[1].fd = sock;
    fds[1].events = POLLIN;

    static char ipc_buffer[16384];
    int initial_draw_done = 0;

    wl_display_flush(ctx->display);

    // init workspace status
    char *json_data = query_hyprland_ipc("workspaces");
    ctx->workspace_count = 0;
    memset(ctx->workspaces, 0, sizeof(ctx->workspaces));
    memset(ctx->workspace_windows, 0, sizeof(ctx->workspace_windows));

    if (json_data) {
        char *ptr = json_data;
        while ((ptr = strstr(ptr, "\"id\":")) != NULL && ctx->workspace_count < 32) {
            int ws_id = 0;
            int windows_count = 0;

            if (sscanf(ptr, "\"id\": %d", &ws_id) == 1) {
                ctx->workspaces[ctx->workspace_count] = ws_id;
                char *win_ptr = strstr(ptr, "\"windows\":");
                if (win_ptr) {
                    sscanf(win_ptr, "\"windows\": %d", &windows_count);
                }
                ctx->workspace_windows[ctx->workspace_count] = windows_count;
                ctx->workspace_count++;
            }
            ptr += 5;
        }
        free(json_data);
    }


    if (ctx->workspace_count == 0) {
        ctx->workspaces[0] = 1;
        ctx->workspace_windows[0] = 0;
        ctx->workspace_count = 1;
        strcpy(ctx->active_workspace, "1");
    }

#ifdef DEBUG
    printf("init done. starting event-loop\n");
#endif
    fflush(stdout);

    while (!ctx->configured && ctx->running) {
        if (wl_display_roundtrip(ctx->display) < 0) {
            ctx->running = 0;
            break;
        }
    }

    initial_draw_done = 1;

    while (ctx->running) {

        // 1: Wayland-Handshake
        while (wl_display_prepare_read(ctx->display) != 0) {
            if (wl_display_dispatch_pending(ctx->display) < 0) {
                ctx->running = 0;
                break;
            }
        }
        if (!ctx->running) break;

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
            wl_display_cancel_read(ctx->display); // Angekündigte Leseabsicht sauber verwerfen

            // Deine vorbereiteten Funktionen ausführen und im Context sichern
            get_iso_time(ctx->sys_time, sizeof(ctx->sys_time));
            get_ram_usage(ctx->sys_ram, sizeof(ctx->sys_ram));
            get_battery_info(ctx->sys_bat, sizeof(ctx->sys_bat));
            ctx->sys_cpu = get_cpu_load();

            // Frame mit den neuen Werten aktualisieren
            if (initial_draw_done) {
                draw_frame(ctx);
            }
            continue; // Direkt zum nächsten Schleifendurchlauf springen
        }

        // 3: wayland event Handling
        if (fds[0].revents & POLLIN) {
            if (wl_display_read_events(ctx->display) < 0) {
                fprintf(stderr, "error at reading wayland events.\n");
                break;
            }
            if (wl_display_dispatch_pending(ctx->display) < 0) {
                break;
            }
        } else {
            wl_display_cancel_read(ctx->display);
        }

        // 4: dynamic desktop-tracking
        if (fds[1].revents & POLLIN) {
            ssize_t len = recv(sock, ipc_buffer, sizeof(ipc_buffer) - 1, 0);
            if (len <= 0) {
                fprintf(stderr, "loosing connection to hyprland.\n");
                break;
            }

            ipc_buffer[len] = '\0'; // String zwingend terminieren
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
                        // open app window
                        else if (strstr(line, "openwindow>>") == line) {
                            char *ws_start = strchr(line, ',');
                            if (ws_start) {
                                int target_ws = atoi(ws_start + 1);
                                int found = 0;

                                for (unsigned short int i = 0; i < ctx->workspace_count; ++i) {
                                    if (ctx->workspaces[i] == target_ws) {
                                        ctx->workspace_windows[i]++;
                                        found = 1;
                                        state_changed = 1;
                                        break;
                                    }
                                }
                                if (!found && ctx->workspace_count < 32) {
                                    ctx->workspaces[ctx->workspace_count] = target_ws;
                                    ctx->workspace_windows[ctx->workspace_count] = 1;
                                    ctx->workspace_count++;
                                    state_changed = 1;
                                }
                            }
                        }
                        // movetoworkspace
                        else if (strstr(line, "movewindowv2>>") == line) {
                            char *ws_start = strchr(line, ',');
                            if (ws_start) {
                                int target_ws = atoi(ws_start + 1);
                                int found = 0;
                                for (unsigned short int i = 0; i < ctx->workspace_count; ++i) {
                                    if (ctx->workspaces[i] == target_ws) {
                                        ctx->workspace_windows[i]++;
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
                        else if (strstr(line, "closewindow>>") == line || strstr(line, "destroyworkspace") == line || strstr(line, "createworkspace") == line) {
                            char *uj = query_hyprland_ipc("workspaces");
                            if (uj) {
                                char *p = uj;
                                int c = 0;
                                while ((p = strstr(p, "\"id\":")) != NULL && c < 32) {
                                    int id = 0; int win = 0;
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

            if (state_changed && initial_draw_done) {
#ifdef DEBUG
                printf("actual frame. Workspace=%s, App=%s\n", ctx->active_workspace, ctx->active_app);
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

    close(sock);
    destroy(ctx);

    return 0;
}
