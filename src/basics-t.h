/**
 *  @file basics-t.h
 *  @brief Defines datatypes for basics.h.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */
#ifndef BASICS_T_H
#define BASICS_T_H

#define _GNU_SOURCE

#include "vector.h"

typedef struct conf_tup_t {
    char *name;
    char *value;
} conf_tup;


typedef struct config_file_t {
    vector conf;
} config_file;

#endif
