#ifndef BASICS_H
#define BASICS_H

#include "types.h"

// checks if the programm runns already
unsigned int checkIfRunning();
// protected against zombie
void zombieProtect();
// shows version informations
void showVersion(char *name, char *version);
// shows help for this programm
void showHelp(char *name);
// opt handling
unsigned int optHandling(int argc, char **argv, struct app_context *ctx);

#endif
