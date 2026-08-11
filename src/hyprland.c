#include "hyprland.h"
#include "types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/un.h>
#include <wordexp.h>
#include <ctype.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>



int get_socket_path(char *path, size_t max_len) {
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    if (!xdg_runtime) return 0;

    const char *env_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (env_sig && strlen(env_sig) > 0) {
        snprintf(path, max_len, "%s/hypr/%s/.socket2.sock", xdg_runtime, env_sig);
        return 1;
    }

    FILE *cmd = popen("hyprctl instances | grep 'instance' | awk '{print $2}' | tr -d ':',", "r");
    if (!cmd) return 0;

    char instance_id[128] = {0};
    if (fgets(instance_id, sizeof(instance_id) - 1, cmd) != NULL) {
        instance_id[strcspn(instance_id, "\r\n")] = 0;
    }
    pclose(cmd);

    if (strlen(instance_id) == 0) return 0;

    snprintf(path, max_len, "%s/hypr/%s/.socket2.sock", xdg_runtime, instance_id);
    return 1;
}



void initial_hyprland_query(struct app_context *ctx) {
    char *json_data = query_hyprland_ipc("workspaces");
    ctx->workspace_count = 0;
    memset(ctx->workspaces, 0, sizeof(ctx->workspaces));
    memset(ctx->workspace_windows, 0, sizeof(ctx->workspace_windows));

    if (json_data) {
        char *ptr = json_data;
        while ((ptr = strstr(ptr, "\"id\":")) != NULL &&
                ctx->workspace_count < 32) {
            int ws_id = 0;
            int windows_count = 0;

            if (sscanf(ptr, "\"id\": %d", &ws_id) == 1) {
                ctx->workspaces[ctx->workspace_count] = ws_id;
                char *win_ptr = strstr(ptr, "\"windows\":");
                if (win_ptr) {
                    sscanf(win_ptr, "\"windows\": %d", &windows_count);
                }
                ctx->workspace_windows[ctx->workspace_count] = windows_count;
                ++ctx->workspace_count;
            }
            ptr += 5;
        }
        free(json_data);
        json_data = NULL;
    }

    json_data = query_hyprland_ipc("activeworkspace");
    if (json_data) {
        char *id_pos = strstr(json_data, "\"id\":");
        if (id_pos) {
            id_pos += 5;
            while (*id_pos == ' ') ++id_pos;
            int idx = 0;
            while (isdigit((unsigned char)id_pos[idx]) && idx < 10) {
                ctx->active_workspace[idx] = id_pos[idx];
                ++idx;
            }
            ctx->active_workspace[idx] = '\0';
        } else {
            strncpy(ctx->active_workspace, json_data, 10);
            ctx->active_workspace[strcspn(ctx->active_workspace, "\r\n")] = '\0';
        }
        free(json_data);
        json_data = NULL;
    }

    // fallback for some previous error
    if (ctx->workspace_count == 0) {
        ctx->workspaces[0] = 1;
        ctx->workspace_windows[0] = 0;
        ctx->workspace_count = 1;
    }
}



int create_hyprland_socket(int *sock) {
    char socket_path[512] = {0};
    int tmp_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    int count = 0;
    const int max_retries = 5;

    while (!get_socket_path(socket_path, sizeof(socket_path)) && count <= max_retries) {
        sleep(1);
        ++count;
    }
    if (tmp_sock < 0 || count == max_retries) return 1;

    count = 0;
    strncpy(addr.sun_path, socket_path, strlen(socket_path)*sizeof(char));
    while (connect(tmp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1 && count <= max_retries) {
        sleep(1);
        ++count;
    }
    if (count == max_retries) return 1;

    *sock = tmp_sock;
    return 0;
}


void fetch_hyprland_colors(struct app_context *ctx) {
    // setting some fallback values
    ctx->bg_r = 0.117; ctx->bg_g = 0.117; ctx->bg_b = 0.180;
    ctx->accent_r = 0.321; ctx->accent_g = 0.443; ctx->accent_b = 0.654;
    ctx->fg_r = 0.9; ctx->fg_g = 0.9; ctx->fg_b = 0.9;

    const char *conf_path = "~/.config/hypr/scheme/current.conf";
    wordexp_t exp_result;
    if (wordexp(conf_path, &exp_result, 0) != 0) return;
    if (exp_result.we_wordc == 0) {
        wordfree(&exp_result);
        return;
    }

    FILE *file = fopen(exp_result.we_wordv[0], "r");
    wordfree(&exp_result);

    if (!file) {
        fprintf(stderr, "[wbar] Warn: cannot read scheme/current.conf . using fallbacks.\n");
        return;
    }

    char line[512];

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        int is_bg = (strstr(line, "$background ") == line || strstr(line, "$base ") == line);
        int is_accent = (strstr(line, "$primary ") == line || strstr(line, "$accent ") == line);
        int is_fg = (strstr(line, "$text ") == line || strstr(line, "$foreground ") == line);

        if (!is_bg && !is_accent && !is_fg) continue;

        // 3. Nach dem Gleichheitszeichen suchen
        char *eq = strchr(line, '=');
        if (!eq) continue;

        char *hex_start = eq + 1;
        // Führende Leerzeichen hinter dem '=' überspringe
        while (*hex_start == ' ' || *hex_start == '\t') ++hex_start;

        // 4. Rohen 6-stelligen Hex-Wert einlesen
        unsigned int hex_val = 0;
        char hex_tmp[7] = {0};
        strncpy(hex_tmp, hex_start, 6); // Kopiert exakt die 6 Farbcodes (z.B. 130d0a)

        if (sscanf(hex_tmp, "%x", &hex_val) == 1) {
            double r = ((hex_val >> 16) & 0xFF) / 255.0;
            double g = ((hex_val >> 8) & 0xFF) / 255.0;
            double b = (hex_val & 0xFF) / 255.0;

            if (is_bg) {
                ctx->bg_r = r; ctx->bg_g = g; ctx->bg_b = b;
            } else if (is_accent) {
                ctx->accent_r = r; ctx->accent_g = g; ctx->accent_b = b;
            } else if (is_fg) {
                ctx->fg_r = r; ctx->fg_g = g; ctx->fg_b = b;
            }
        }
    }

    fclose(file);
#ifdef DEBUG
    printf("[wbar] colors extracted from current.conf\n");
    printf("[wbar] -> BG:(%.2f, %.2f, %.2f) Accent:(%.2f, %.2f, %.2f) FG:(%.2f, %.2f, %.2f)\n",
           ctx->bg_r, ctx->bg_g, ctx->bg_b, ctx->accent_r, ctx->accent_g, ctx->accent_b, ctx->fg_r, ctx->fg_g, ctx->fg_b);
#endif
    fflush(stdout);
}




char *query_hyprland_ipc(const char *command) {
    char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) return NULL;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/run/user/%d/hypr/%s/.socket.sock", getuid(), sig);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return NULL;
    }

    char cmd_formatted[256];
    snprintf(cmd_formatted, sizeof(cmd_formatted), "[[j]]/%s", command);

    if (write(sock, cmd_formatted, strlen(cmd_formatted)) < 0) {
        close(sock);
        return NULL;
    }

    size_t buf_size = 4096;
    char *res_buf = malloc(buf_size);
    if (!res_buf) { close(sock); return NULL; }

    size_t total_read = 0;
    char chunk[1024];
    ssize_t bytes_read;

    while ((bytes_read = read(sock, chunk, sizeof(chunk))) > 0) {
        if (total_read + bytes_read >= buf_size) {
            buf_size *= 2;
            char *new_buf = realloc(res_buf, buf_size);
            if (!new_buf) { free(res_buf); close(sock); return NULL; }
            res_buf = new_buf;
        }
        memcpy(res_buf + total_read, chunk, bytes_read);
        total_read += bytes_read;
    }
    res_buf[total_read] = '\0';
    close(sock);

    return res_buf;
}



void send_hyprland_cmd(const char *command) {
    char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) return;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/run/user/%d/hypr/%s/.socket.sock", getuid(), sig);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    if (write(sock, command, strlen(command)) < 0) {
        perror("[wbar] Fehler beim Schreiben in den Socket");
    }

    close(sock);
}
