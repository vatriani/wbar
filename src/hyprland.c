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



void initial_hyprland_query(hyprland *ctx) {
    ctx->workspaces_count = 0;

    for (int i = 0; i < MAX_WORKSPACES; ++i) {
        ctx->workspaces[i].id = 0;
        ctx->workspaces[i].window_count = 0;
    }

    char *json_data = query_hyprland_ipc("[[BATCH]]j/workspaces; j/activeworkspace; j/activewindow");
// 0. splitting string
    if (json_data) {
        char *workspaces_part = json_data;
        char *active_ws_part = NULL;
        char *active_win_part = NULL;

        char *split1 = strstr(json_data, "\n{");
        if (split1) {
            *split1 = '\0';
            active_ws_part = split1 + 1;

            char *split2 = strstr(active_ws_part, "\n{");
            if (split2) {
                *split2 = '\0';
                active_win_part = split2 + 1;
            }
        }
// 1. WORKSPACES PARSING
        char *ptr = workspaces_part;
        while ((ptr = strstr(ptr, "{")) != NULL && ctx->workspaces_count < MAX_WORKSPACES) {
            char id_str[16] = {0};
            char win_str[16] = {0};

            get_json_value(ptr, "id", id_str, sizeof(id_str));
            get_json_value(ptr, "windows", win_str, sizeof(win_str));

            if (id_str[0] != '\0') {
                ctx->workspaces[ctx->workspaces_count].id = atoi(id_str);
                ctx->workspaces[ctx->workspaces_count].window_count = atoi(win_str);
                ctx->workspaces_count++;
            }
            ++ptr;
        }
// 2. ACTIVE WORKSPACE PARSING
        if (active_ws_part) {
            get_json_value(active_ws_part, "id", ctx->active_workspace, 10);

            if (ctx->active_workspace[0] == '\0') {
                strncpy(ctx->active_workspace, active_ws_part, 10);
                ctx->active_workspace[strcspn(ctx->active_workspace, "\r\n")] = '\0';
            }
        }
// 3. ACTIVE WINDOW PARSSING
        if (active_win_part) {
            char app_class[256] = {0};
            char app_title[256] = {0};

            get_json_value(active_win_part, "class", app_class, sizeof(app_class));
            get_json_value(active_win_part, "title", app_title, sizeof(app_title));

            if (app_class[0] != '\0') {
                if (app_title[0] != '\0') {
                    snprintf(ctx->active_app, MAX_APP_NAME_LENGTH - 1, "%s - %s", app_class, app_title);
                } else {
                    strncpy(ctx->active_app, app_class, MAX_APP_NAME_LENGTH - 1);
                }
            } else {
                if (app_title[0] != '\0') {
                    strncpy(ctx->active_app, app_title, MAX_APP_NAME_LENGTH - 1);
                } else {
                    ctx->active_app[0] = '\0';
                }
            }
        } else {
            ctx->active_app[0] = '\0';
        }

        free(json_data);
        json_data = NULL;
    } else {
        ctx->active_app[0] = '\0';
    }

}



int create_hyprland_socket(hyprland *ctx) {
    char socket_path[512] = {0};
    ctx->socket2_fd = socket(AF_UNIX, SOCK_STREAM, 0);
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
    if (ctx->socket2_fd < 0 || count == max_retries) return 1;

    count = 0;
    strncpy(addr.sun_path, socket_path, strlen(socket_path)*sizeof(char) + 1);
    addr.sun_path[strlen(socket_path) + 1] = '\0';
    while (connect(ctx->socket2_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1 &&
            count <= max_retries) {
        sleep(1);
        ++count;
    }
    if (count == max_retries) return 1;

    return 0;
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

    char dump[128];
    while (recv(sock, dump, sizeof(dump), MSG_DONTWAIT) > 0);

    char cmd_formatted[512];
    if (strncmp(command, "[[BATCH]]", 9) == 0) {
        snprintf(cmd_formatted, sizeof(cmd_formatted), "%s", command);
    } else {
        snprintf(cmd_formatted, sizeof(cmd_formatted), "[[j]]/%s", command);
    }

    if (write(sock, cmd_formatted, strlen(cmd_formatted)) < 0) {
        close(sock);
        return NULL;
    }

    size_t buf_size = 4096;
    char *res_buf = malloc(buf_size);
    if (!res_buf) {
        close(sock);
        return NULL;
    }

    size_t total_read = 0;
    char chunk[MAX_APP_NAME_LENGTH];
    ssize_t bytes_read;

    while ((bytes_read = read(sock, chunk, sizeof(chunk))) > 0) {
        if (total_read + bytes_read >= buf_size) {
            buf_size *= 2;
            char *new_buf = realloc(res_buf, buf_size);
            if (!new_buf) {
                free(res_buf);
                close(sock);
                return NULL;
            }
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

    char temp[MAX_APP_NAME_LENGTH];
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
