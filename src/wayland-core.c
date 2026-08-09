#include "wayland-core.h"
#include "buffer.h"
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
#include <linux/input-event-codes.h>
#include <wayland-client.h>


static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    (void)data; (void)seat; (void)capabilities;
}



static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}


static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};



void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    register struct app_context *ctx = (struct app_context *)data;

    if (!ctx) return;

    if (strncmp(interface, "wl_compositor", 13) == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, bind_ver);
    } else if (strncmp(interface, "zwlr_layer_shell_v1", 19) == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, bind_ver);
    } else if (strncmp(interface, "wl_shm", 6) == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, bind_ver);
    } else if (strncmp(interface, "wl_seat", 7) == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->seat = wl_registry_bind(registry, id, &wl_seat_interface, bind_ver);
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
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
    ctx->bg_r = 0.117; ctx->bg_g = 0.117; ctx->bg_b = 0.180;
    ctx->accent_r = 0.321; ctx->accent_g = 0.443; ctx->accent_b = 0.654;

    char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) return;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/hypr/%s/.socket.sock", sig);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    const char *cmd = "[[j]]/getoption general:col.active_border";
    if (write(sock, cmd, strlen(cmd)) < 0) { close(sock); return; }

    char buf[1024];
    int len = read(sock, buf, sizeof(buf) - 1);
    close(sock);

    if (len <= 0) return;
    buf[len] = '\0';

    register char *hex_start = strstr(buf, "\"str\": \"");
    if (!hex_start) return;
    hex_start += 8;

    if (*hex_start == '0' && *(hex_start + 1) == 'x') hex_start += 2;
    if (*hex_start == '#') hex_start += 1;

    if (strlen(hex_start) >= 6) {
        unsigned int hex_val = 0;
        if (strlen(hex_start) >= 8) sscanf(hex_start + 2, "%6x", &hex_val);
        else sscanf(hex_start, "%6x", &hex_val);

        ctx->accent_r = ((hex_val >> 16) & 0xFF) / 255.0;
        ctx->accent_g = ((hex_val >> 8) & 0xFF) / 255.0;
        ctx->accent_b = (hex_val & 0xFF) / 255.0;

        ctx->bg_r = ctx->accent_r * 0.25; ctx->bg_g = ctx->accent_g * 0.25; ctx->bg_b = ctx->accent_b * 0.25;
        if (ctx->bg_r > 0.15) ctx->bg_r = 0.117;
        if (ctx->bg_g > 0.15) ctx->bg_g = 0.117;
        if (ctx->bg_b > 0.15) ctx->bg_b = 0.15;
    }
}
