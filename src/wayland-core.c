#include "wayland-core.h"
#include "buffer.h"
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



static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    (void)data; (void)seat; (void)capabilities;
}



static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}


const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};



void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    struct app_context *ctx = (struct app_context *)data;
    if (!ctx) return;

    if (strcmp(interface, "wl_compositor") == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, bind_ver);
    } else if (strcmp(interface, "zwlr_layer_shell_v1") == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, bind_ver);
    } else if (strcmp(interface, "wl_shm") == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, bind_ver);
    } else if (strcmp(interface, "wl_seat") == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->seat = wl_registry_bind(registry, id, &wl_seat_interface, bind_ver);
    }
}



void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}



const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};



void fetch_hyprland_colors(struct app_context *ctx) {
    // 1. Sichere Standardwerte setzen, falls die Datei fehlt
    ctx->bg_r = 0.117; ctx->bg_g = 0.117; ctx->bg_b = 0.180;
    ctx->accent_r = 0.321; ctx->accent_g = 0.443; ctx->accent_b = 0.654;
    ctx->fg_r = 0.9; ctx->fg_g = 0.9; ctx->fg_b = 0.9;

    const char *conf_path = "~/.config/hypr/scheme/current.conf";
    wordexp_t exp_result;
    if (wordexp(conf_path, &exp_result, 0) != 0) {
        return;
    }
    if (exp_result.we_wordc == 0) {
        wordfree(&exp_result);
        return;
    }

    FILE *file = fopen(exp_result.we_wordv[0], "r");
    wordfree(&exp_result);

    if (!file) {
        fprintf(stderr, "[wbar] Warnung: Konnte scheme/current.conf nicht öffnen. Nutze Defaults.\n");
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
        // Führende Leerzeichen hinter dem '=' überspringen
        while (*hex_start == ' ' || *hex_start == '\t') {
            hex_start++;
        }

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
    printf("[wbar] Farben erfolgreich aus current.conf extrahiert!\n");
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

    // Wir schreiben den fertigen Befehl (der das \n bereits enthält) direkt raus
    if (write(sock, command, strlen(command)) < 0) {
        perror("[wbar] Fehler beim Schreiben in den Socket");
    }

    // Direkt schließen, um Hyprland zur sofortigen Ausführung zu zwingen
    close(sock);
}
