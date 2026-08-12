#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>



void get_battery_info(char *dest, size_t max_len) {
    FILE *f_cap = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    FILE *f_stat = fopen("/sys/class/power_supply/BAT0/status", "r");

    if (!f_cap || !f_stat) {
        if (f_cap) fclose(f_cap);
        if (f_stat) fclose(f_stat);
        snprintf(dest, max_len, "BAT N/A");
        return;
    }

    int capacity = 0;
    char status[32] = {0};

    if (fscanf(f_cap, "%d", &capacity) != 1) capacity = 0;
    if (fgets(status, sizeof(status) - 1, f_stat) == NULL)
            strcpy(status, "Unknown");

    fclose(f_cap);
    fclose(f_stat);

    char *icon = "d";
    if (strstr(status, "Charging")) {
        icon = "c";
    }

    snprintf(dest, max_len, "%s %d%%", icon, capacity);
}



void get_ram_usage(char *dest, size_t max_len) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        snprintf(dest, max_len, "RAM N/A");
        return;
    }

    long total = 0, available = 0;
    char label[64];
    long value;

    while (fscanf(fp, "%63s %ld kB", label, &value) == 2) {
        if (strcmp(label, "MemTotal:") == 0) total = value;
        if (strcmp(label, "MemAvailable:") == 0) {
            available = value;
            break;
        }
    }
    fclose(fp);

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



int get_cpu_load() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0;

    unsigned long long user, nice, system, idle;

    if (fscanf(fp, "cpu %llu %llu %llu %llu", &user, &nice, &system, &idle)
            != 4) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

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
