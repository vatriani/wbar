#define _GNU_SOURCE


#include "basics.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/un.h>
#include <getopt.h>



unsigned int checkIfRunning() {
    int instance_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (instance_sock >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        strncpy(addr.sun_path + 1, "wbar_single_instance_lock", sizeof(addr.sun_path) - 2);

        if (bind(instance_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (errno == EADDRINUSE) {
                fprintf(stderr, "wbar: is already running\n");
                close(instance_sock);
                return 1;
            }
        }
    }
#ifdef DEBUG
    printf("check for single instance - passed\n");
#endif
    return 0;
}



inline void zombieProtect() {
    signal(SIGCHLD, SIG_IGN);
#ifdef DEBUG
    printf("zombieprotection - passed\n");
#endif
}



inline void showVersion(char *name, char *version) {
    printf("%s - %s (c) vatriani 2026\n", name, version);
}



inline void showHelp(char *name) {
    printf("usage: %s [OPTIONS]...\n\n -h  shows help\n -v  shows version\n", name);
}



unsigned int optHandling( int argc, char **argv, struct app_context *ctx) {
    while (1) {
        int opt = 0;
        int option_index = 0;
        static struct option long_options[] = {
            { "help", no_argument, 0, 'h'},
            { "version", no_argument, 0, 'v'},
            { "font", required_argument, 0, 'f'},
            { 0, 0, 0, 0},
        };

        opt = getopt_long (argc, argv, "hvf", long_options, &option_index);

        if (opt == -1) return 1;

        switch (opt) {
            case 'h':
                showHelp(argv[0]);
                return 1;
            case 'v':
                showVersion(argv[0], "b0.1");
                return 1;
            case 'f':
                strcpy(ctx->font, optarg);
                return 1;
        }
    }
#ifdef DEBUG
    printf("opthandling - passed ");
#endif
        return 0;
}
