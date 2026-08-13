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
        snprintf(path, max_len, "%s/hypr/%s/.socket2.sock",
                xdg_runtime, env_sig);
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

    snprintf(path, max_len, "%s/hypr/%s/.socket2.sock", xdg_runtime,
            instance_id);
    return 1;
}



int get_json_value(const char *json, const char *key, char *dest,
        size_t dest_size) {
    if (!json || !key || !dest || dest_size == 0) return 0;
    dest[0] = '\0';

    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);

    const char *pos = strstr(json, search_key);
    if (!pos) return 0;

    pos = strchr(pos, ':');
    if (!pos) return 0;
    ++pos;

    while (*pos == ' ' || *pos == '"') ++pos;

    size_t idx = 0;
    while (*pos != '\0' && *pos != ',' && *pos != '}' && *pos != ']' &&
            *pos != '"' && *pos != '\n' && *pos != '\r' &&
            idx < (dest_size - 1)) {
        if (*pos == '\\' && *(pos + 1) == '"') ++pos;
        dest[idx++] = *pos++;
    }

    while (idx > 0 && dest[idx - 1] == ' ') --idx;

    dest[idx] = '\0';
    return (idx > 0);
}



void initial_hyprland_query(struct app_context *ctx) {
    ctx->hypr.workspaces_count = 0;

    for (int i = 0; i < MAX_WORKSPACES; ++i) {
        ctx->hypr.workspaces[i].id = 0;
        ctx->hypr.workspaces[i].window_count = 0;
    }

    // 1. WORKSPACES PARSEN
    char *json_data = query_hyprland_ipc("workspaces");
    if (json_data) {
        char *ptr = json_data;
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
        json_data = NULL;
    }

    // 2. ACTIVE WORKSPACE PARSEN
    json_data = query_hyprland_ipc("activeworkspace");
    if (json_data) {
        get_json_value(json_data, "id", ctx->hypr.active_workspace, 10);

        if (ctx->hypr.active_workspace[0] == '\0') {
            strncpy(ctx->hypr.active_workspace, json_data, 10);
            ctx->hypr.active_workspace[strcspn(ctx->hypr.active_workspace,
                    "\r\n")] = '\0';
        }
        free(json_data);
        json_data = NULL;
    }

    // 3. ACTIVE WINDOW PARSEN
    json_data = query_hyprland_ipc("activewindow");
    if (json_data) {
        char app_class[256] = {0};
        char app_title[256] = {0};

        get_json_value(json_data, "class", app_class, sizeof(app_class));
        get_json_value(json_data, "title", app_title, sizeof(app_title));

        if (app_class[0] != '\0') {
            if (app_title[0] != '\0') snprintf(ctx->hypr.active_app, MAX_APP_NAME_LENGTH -1,
                    "%s - %s", app_class, app_title);
            else strncpy(ctx->hypr.active_app, app_class, MAX_APP_NAME_LENGTH - 1);
        }
        else {
            if (app_title[0] != '\0') strncpy(ctx->hypr.active_app, app_title, MAX_APP_NAME_LENGTH - 1);
            else ctx->hypr.active_app[0] = '\0';
        }
        free(json_data);
        json_data = NULL;
    }
    else ctx->hypr.active_app[0] = '\0';
}



int create_hyprland_socket(int *sock) {
    char socket_path[512] = {0};
    int tmp_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    int count = 0;
    const int max_retries = 5;

    while (!get_socket_path(socket_path, sizeof(socket_path)) &&
            count <= max_retries) {
        sleep(1);
        ++count;
    }
    if (tmp_sock < 0 || count == max_retries) return 1;

    count = 0;
    strncpy(addr.sun_path, socket_path, strlen(socket_path)*sizeof(char));
    while (connect(tmp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1 &&
            count <= max_retries) {
        sleep(1);
        ++count;
    }
    if (count == max_retries) return 1;

    *sock = tmp_sock;
    return 0;
}


void fetch_hyprland_colors(struct app_context *ctx) {
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
        fprintf(stderr, "[wbar] Warn: cannot read scheme/current.conf. using fallbacks.\n");
        return;
    }

    char line[512];

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        int is_bg = (strstr(line, "$background ") == line
                || strstr(line, "$base ") == line);
        int is_accent = (strstr(line, "$primary ") == line
                || strstr(line, "$accent ") == line);
        int is_fg = (strstr(line, "$text ") == line
                || strstr(line, "$foreground ") == line);

        if (!is_bg && !is_accent && !is_fg) continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        char *hex_start = eq + 1;

        while (*hex_start == ' ' || *hex_start == '\t') ++hex_start;

        unsigned int hex_val = 0;
        char hex_tmp[7] = {0};
        strncpy(hex_tmp, hex_start, 6);

        if (sscanf(hex_tmp, "%x", &hex_val) == 1) {
            double r = ((hex_val >> 16) & 0xFF) / 255.0;
            double g = ((hex_val >> 8) & 0xFF) / 255.0;
            double b = (hex_val & 0xFF) / 255.0;

            if (is_bg) {
                ctx->render.bg_color.r = r;
                ctx->render.bg_color.g = g;
                ctx->render.bg_color.b = b;
            } else if (is_accent) {
                ctx->render.accent_color.r = r;
                ctx->render.accent_color.g = g;
                ctx->render.accent_color.b = b;
            } else if (is_fg) {
                ctx->render.fg_color.r = r;
                ctx->render.fg_color.g = g;
                ctx->render.fg_color.b = b;
            }
        }
    }

    fclose(file);
#ifdef DEBUG
    printf("[wbar] colors extracted from current.conf\n");
    printf("[wbar] -> BG:(%.2f, %.2f, %.2f) Accent:(%.2f, %.2f, %.2f) FG:(%.2f, %.2f, %.2f)\n",
           ctx->render.bg_color.r, ctx->render.bg_color.g, ctx->render.bg_color.b,
           ctx->render.accent_color.r, ctx->render.accent_color.g, ctx->render.accent_color.b,
           ctx->render.fg_color.r, ctx->render.fg_color.g, ctx->render.fg_color.b);
    fflush(stdout);
#endif
}




char *query_hyprland_ipc(const char *command) {
    char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) return NULL;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path),
            "/run/user/%d/hypr/%s/.socket.sock", getuid(), sig);

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
    snprintf(addr.sun_path, sizeof(addr.sun_path),
            "/run/user/%d/hypr/%s/.socket.sock", getuid(), sig);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    if (write(sock, command, strlen(command)) < 0)
        perror("[wbar] Fehler beim Schreiben in den Socket");

    close(sock);
}



void parse_hyprland_app_name(const char *raw_data, char *dest,
        size_t dest_size) {
    if (!raw_data || !dest || dest_size == 0) return;

    char temp[1024];
    strncpy(temp, raw_data, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *comma = strchr(temp, ',');
    if (comma) {
        *comma = '\0';
        char *app_class = temp;
        char *app_title = comma + 1;

        snprintf(dest, dest_size, "%s - %s", app_class, app_title);
    } else {
        strncpy(dest, temp, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
}
