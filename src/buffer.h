/**
 *  @file buffer.h
 *  @brief Defines some functions to encapsulate drawing functionality.
 *  @author N. Neumann
 *  @version 0.2
 *  @date 2026
 *  @copyright GPLv3
 */
#ifndef BUFFER_H
#define BUFFER_H

#define _GNU_SOURCE

#include "types.h"

/**
 * @brief Initialize all objects for rendering and drawing in the app_context.
 *
 * This function checks if any opts are given at exec. if so, handles them.
 *
 * @param ctx Pass the address of an app_context strucht.
 * @return 0 on success. -1 need program quit.
 *
 * @note Pass argc and argv from the main() directly. Beware of the ctx struct.
 */
int init_rendering(struct app_context *ctx);

/**
 * @brief Helperfunction to free savely render struct in the app_context.
 *
 * @param ctx Pass the address of an app_context strucht.
 */
void cleanup_rendering(struct app_context *ctx);

/**
 * @brief Function holds the implementation of an drawing.
 *
 * This function implements the dirty rectangle logic and is triggered from the
 * main loop, everytime something needs to render or update.
 *
 * @param ctx Pass the address of an app_context strucht.
 *
 * @see void draw_segment_left(struct app_context *ctx)
 *      void draw_segment_middle(struct app_context *ctx)
 *      void draw_segment_right(struct app_context *ctx)
 *
 * @note uint32_t changed_segments in app_context must be set to changed dirty
 *       rectangle. Multiple bits are supported.
 */
void draw_frame(struct app_context *ctx);

/**
 * @brief Implements simple color code transformation.
 *
 * @param tmp String contains "#FFFFFF" or without asterix at the beginning.
 * @return A color struct with the converted collor codes. When NULL an error
 *         occured.
 *
 * IMPROVEMENT Needs to be harden at incorrect input strings.
 */
color rgb_to_double(char *tmp);

#endif
