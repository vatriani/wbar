/**
 *  @file sys-vitals.h
 *  @brief Defines some functions to encapsulate fetching systemvitals.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */

#ifndef SYS_VITALS_H
#define SYS_VITALS_H

#define _GNU_SOURCE

#include <stdio.h>


#define MAX_BUFF_SYS 32


typedef struct sys_vitals_t sys_vitals;
struct sys_vitals_t {
    FILE *cpu_fp;
    FILE *mem_fp;
    FILE *bat_cap_fp;
    FILE *bat_stat_fp;
    int bat_available;
    char sys_time[MAX_BUFF_SYS];
    char sys_ram[MAX_BUFF_SYS];
    char sys_bat[MAX_BUFF_SYS];
    int  sys_cpu;
};



int create_sysvitals_fd(sys_vitals *ctx);
int get_cpu_load(sys_vitals *ctx);
void get_ram_usage(sys_vitals *ctx, char *dest, size_t max_len);
void get_battery_info(sys_vitals *ctx, char *dest, size_t max_len);
void get_iso_time(char *dest, size_t max_len);
int cleanup_sysvitals(sys_vitals *ctx);
#endif
