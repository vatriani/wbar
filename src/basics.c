#include "basics.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <getopt.h>
#include <ctype.h>



/**
 * Resolves the absolute path to the user's XDG configuration directory.
 * If XDG_CONFIG_HOME is empty/unset, it falls back to $HOME/.config.
 *
 * @param out_path Buffer to store the resulting path.
 * @param max_len Size of the output buffer.
 * @return 0 on success, -1 on failure (buffer overflow or missing $HOME).
 */
int get_xdg_config_path(char *out_path, size_t max_len) {
    const char *xdg_config = getenv("XDG_CONFIG_HOME");

    if (xdg_config && xdg_config[0] != '\0') {
        if (snprintf(out_path, max_len, "%s/%s", xdg_config, APP_NAME) >= (ssize_t)max_len) {
            return -1; // Buffer too small
        }
        return 0;
    }

    // Fall back to $HOME/.config
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') return -1;

    // Safely concatenate $HOME and "/.config"
    if (snprintf(out_path, max_len, "%s/.config/%s", home, APP_NAME) >= (ssize_t)max_len) {
        return -1;
    }

    return 0;
}



// Hilfsfunktion für qsort: Vergleicht zwei void** Elemente, die conf_tup_t* enthalten
int compare_config_tups(const void *a, const void *b) {
    const conf_tup *tup_a = *(const conf_tup **)a;
    const conf_tup *tup_b = *(const conf_tup **)b;
    return strcmp(tup_a->name, tup_b->name);
}



// Hilfsfunktion für bsearch: Sucht nach dem String-Schlüssel
int compare_search_key(const void *key, const void *element) {
    const char *search_name = (const char *)key;
    const conf_tup *tup = *(const conf_tup **)element;
    return strcmp(search_name, tup->name);
}



// Hilfsfunktion zum Abschneiden von Whitespaces
char *trim_spaces(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}



int configParse(config_file *cfg, FILE **file) {
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, *file)) != -1) {
        char *trimmed = trim_spaces(line);

        // Kommentare (# oder ;) und Leerzeilen überspringen
        if (line[0] == '#' || line[0] == '\n' ||
                line[0] == '\r' || line[0] == ';') continue;

        // Trennung beim ersten Gleichheitszeichen
        char *delimiter = strchr(trimmed, '=');
        if (!delimiter) continue;

        *delimiter = '\0';
        char *name = trim_spaces(trimmed);
        char *value = trim_spaces(delimiter + 1);

        conf_tup *tup = malloc(sizeof(conf_tup));
        if (!tup || !(tup->name = strdup(name)) || !(tup->value = strdup(value))) {
            if (tup) free(tup);
            return -1;  // oder error handling
        }
        tup->name = strdup(name);
        tup->value = strdup(value);

        if (cfg->conf.pfVectorAdd(&cfg->conf, tup)) return -1;
    }
    free(line);

    // Einmaliges Sortieren für die O(log n) Binärsuche
    int total_elements = cfg->conf.pfVectorTotal(&cfg->conf);
    if (total_elements > 0)
        qsort(cfg->conf.vectorList.items, total_elements, sizeof(void *), compare_config_tups);
    return 0;
}



int configLoad(config_file *cf) {
    char filepath[MAX_PATH];
    filepath[0] = '\0';
    vector_init(&cf->conf);
    const char *filename = "/config.cfg";

    if (get_xdg_config_path(filepath, MAX_PATH) != 0) {
        fprintf(stderr, "[wbar] Warn: cannot find create XDG_CONFIG_DIR\n");
        return -1;
    }

    strncat(filepath, filename, strlen(filename)+1);

    FILE *file = fopen(filepath, "r");

    if (!file) {
        fprintf(stderr, "[wbar] Warn: cannot open wbar/config.cfg.\n");
        return -1;
    }

    if(configParse(cf, &file) != 0) {
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}



char* configGetValueFromName(config_file *cfg, const char *name) {
    if (!cfg) return NULL;

    int total_elements = cfg->conf.pfVectorTotal(&cfg->conf);
    if (total_elements == 0) return NULL;

    // Binäre Suche direkt auf dem internen Array deines Vektors
    conf_tup **found = bsearch(name,
                                 cfg->conf.vectorList.items,
                                 total_elements,
                                 sizeof(void *),
                                 compare_search_key);

    return found ? (*found)->value : NULL;
}



// cleanup
void configFree(config_file *cfg) {
    if (!cfg) return;

    int total_elements = cfg->conf.pfVectorTotal(&cfg->conf);

    for (int i = 0; i < total_elements; i++) {
        conf_tup *tup = (conf_tup *)cfg->conf.pfVectorGet(&cfg->conf, i);
        if (tup) {
            free(tup->name);
            free(tup->value);
            free(tup);
        }
    }

    cfg->conf.pfVectorFree(&cfg->conf);
}



int checkIfRunning() {
    int instance_sock = socket(AF_UNIX, SOCK_STREAM, 0);

    if (instance_sock >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path + 1, "wbar_single_instance_lock",
                sizeof(addr.sun_path) - 2);

        if (bind(instance_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (errno == EADDRINUSE) {
                fprintf(stderr, "wbar: is already running\n");
                close(instance_sock);
                return 1;
            }
        }
    }
    return 0;
}



inline void zombieProtect() {
    signal(SIGCHLD, SIG_IGN);
}



inline void showVersion(char *name, char *version) {
    printf("%s - %s (c) vatriani 2026\n", name, version);
}



inline void showHelp(char *name) {
    printf("usage: %s [OPTIONS]...\n \
            \n -h  shows help\n -v  shows version\n", name);
}



unsigned int optHandling(int argc, char **argv, struct app_context *ctx) {
    while (1) {
        int opt = 0;
        int option_index = 0;
        static struct option long_options[] = {
            { "help", no_argument, 0, 'h'},
            { "version", no_argument, 0, 'v'},
            { "font", required_argument, 0, 'f'},
            { 0, 0, 0, 0},
        };

        opt = getopt_long(argc, argv, "hvf", long_options, &option_index);
        if (opt == -1) return 0;

        switch (opt) {
            case 'h':
                showHelp(argv[0]);
                return 1;
            case 'v':
                showVersion(argv[0], "b0.1");
                return 1;
            case 'f':
                strncpy(ctx->render.font, optarg, MAX_APP_NAME_LENGTH - 1);
                ctx->render.font[MAX_APP_NAME_LENGTH - 1] = '\0';
                break;
        }
    }
    return 0;
}
