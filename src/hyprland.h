/**
 *  @file hyprland.h
 *  @brief Defines some functions to encapsulate hyprland behaviour.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */
#ifndef HYPRLAND_H
#define HYPRLAND_H

#define _GNU_SOURCE

#include <stddef.h>

#define MAX_WORKSPACES             32

typedef struct workspace_t workspace;
struct workspace_t {
    int id;
    int window_count;
};

typedef struct hyprland_context hyprland;
struct hyprland_context {
    int        socket2_fd;
    char      *active_workspace;
    char      *active_app;
    workspace  workspaces[MAX_WORKSPACES];
    int        workspaces_count;
};

int create_hyprland_socket(hyprland *ctx);
void initial_hyprland_query(hyprland *ctx);

int get_json_value(const char *json, const char *key, char *dest, size_t dest_size);
char *query_hyprland_ipc(const char *command);
void send_hyprland_cmd(const char *command);
void parse_hyprland_app_name(const char *raw, char *dest, size_t dest_size);

#endif
