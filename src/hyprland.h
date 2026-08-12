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

#include "types.h"

int create_hyprland_socket(int *sock);
void initial_hyprland_query(struct app_context *ctx);
void fetch_hyprland_colors(struct app_context *ctx);
char *query_hyprland_ipc(const char *command);
void send_hyprland_cmd(const char *command);
void parse_hyprland_app_name(const char *raw, char *dest, size_t dest_size);

#endif
