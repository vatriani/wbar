#include "sys-vitals.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>



void get_battery_info(sys_vitals *ctx, char *dest, size_t max_len) {
    if (!ctx->bat_available) {
        snprintf(dest, max_len, "BAT N/A");
        return;
    }

    int capacity = 0;
    char status[32] = {0};

    fflush(ctx->bat_cap_fp);
    rewind(ctx->bat_cap_fp);
    fflush(ctx->bat_stat_fp);
    rewind(ctx->bat_stat_fp);
    if (fscanf(ctx->bat_cap_fp, "%d", &capacity) != 1) capacity = 0;
    if (fgets(status, sizeof(status) - 1, ctx->bat_stat_fp) == NULL) {
        strncpy(status, "Unknown", 32*sizeof(char));
        status[32-1] = '\0';
    }

    char *icon = "d";
    if (strstr(status, "Charging")) {
        icon = "c";
    }

    snprintf(dest, max_len, "%s %d%%", icon, capacity);
}



void get_ram_usage(sys_vitals *ctx, char *dest, size_t max_len) {
    long total = 0, available = 0;
    char label[64];
    long value;

    fflush(ctx->mem_fp);
    rewind(ctx->mem_fp);
    while (fscanf(ctx->mem_fp, "%63s %ld kB", label, &value) == 2) {
        if (strcmp(label, "MemTotal:") == 0) total = value;
        if (strcmp(label, "MemAvailable:") == 0) {
            available = value;
            break;
        }
    }

    if (total > 0) {
        long used = total - available;
        int percent = (int)((used * 100) / total);
        snprintf(dest, max_len, "RAM: %d%%", percent);
    } else {
        snprintf(dest, max_len, "RAM ERR");
    }
}



// Globale Variablen für den vorherigen CPU-Status
unsigned long long prev_user, prev_nice, prev_system, prev_idle;



int get_cpu_load(sys_vitals *ctx) {
    unsigned long long user, nice, system, idle;

    fflush(ctx->cpu_fp);
    rewind(ctx->cpu_fp);
    if (fscanf(ctx->cpu_fp, "cpu %llu %llu %llu %llu", &user, &nice, &system, &idle)
            != 4) {
        return 0;
    }

    unsigned long long prev_total = prev_user + prev_nice + prev_system +
            prev_idle;
    unsigned long long current_total = user + nice + system + idle;

    unsigned long long total_delta = current_total - prev_total;
    unsigned long long idle_delta = idle - prev_idle;

    int percent = 0;
    if (total_delta > 0) {
        percent = (int)(100 * (total_delta - idle_delta) / total_delta);
    }

    prev_user = user; prev_nice = nice; prev_system = system; prev_idle = idle;

    return percent;
}



void get_iso_time(char *dest, size_t max_len) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);

    strftime(dest, max_len, "%Y-%m-%d - %H:%M", tm_info);
}



int create_sysvitals_fd(sys_vitals *ctx) {
    ctx->cpu_fp = NULL;
    ctx->bat_cap_fp = NULL;
    ctx->bat_stat_fp = NULL;
    ctx->bat_available = 0;
    ctx->mem_fp = NULL;

    ctx->cpu_fp = fopen("/proc/stat", "r");
    if (!ctx->cpu_fp) return -1;

    ctx->bat_cap_fp = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    ctx->bat_stat_fp = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (ctx->bat_stat_fp != NULL && ctx->bat_cap_fp != NULL)
        ctx->bat_available = 1;

    ctx->mem_fp = fopen("/proc/meminfo", "r");
    if (!ctx->mem_fp) return -1;

    return 0;
}



int cleanup_sysvitals(sys_vitals *ctx) {
    if (ctx->cpu_fp) fclose(ctx->cpu_fp);
    if (ctx->bat_cap_fp) fclose(ctx->bat_cap_fp);
    if (ctx->bat_stat_fp) fclose(ctx->bat_stat_fp);
    if (ctx->mem_fp) fclose(ctx->mem_fp);

    return 0;
}
