/**
 *  @file buffer.h
 *  @brief Defines some functions to encapsulate drawing functionality.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */

#ifndef BUFFER_H
#define BUFFER_H

#define _GNU_SOURCE

#include "types.h"

int init_rendering(struct app_context *ctx);
void cleanup_rendering(struct app_context *ctx);
void draw_frame(struct app_context *ctx);
color rgb_to_double(char *tmp);

#endif
