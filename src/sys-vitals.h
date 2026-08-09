#ifndef SYS_VITALS_H
#define SYS_VITALS_H

#include <stddef.h>

int get_cpu_load();
void get_ram_usage(char *dest, size_t max_len);
void get_battery_info(char *dest, size_t max_len);
void get_iso_time(char *dest, size_t max_len);
#endif
