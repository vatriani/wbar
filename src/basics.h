/**
 *  @file basics.h
 *  @brief Defines some functions to encapsulate basic program behaviour.
 *  @author N. Neumann
 *  @version 0.1
 *  @date 2026
 *  @copyright GPLv3
 */
#ifndef BASICS_H
#define BASICS_H

#define _GNU_SOURCE

#include "basics-t.h"
#include "types.h"


#define MAX_PATH 1024
#define MAX_CONFIG_LINE_LENGTH 256

int configLoad(config_file *cf);
void configFree(config_file *cfg);
char* configGetValueFromName(config_file *cf, const char *name);

/**
 * @brief Checks if the programm runns already to prevent double instances.
 *
 * This function checks if the programm already runns. Its a one shot
 * function somewhere at programm start.
 *
 * @return 0 == this is the first instance. 1 == another instance is found
 */
int checkIfRunning();

/**
 * @brief Simple zombie protection for this programm
 */
void zombieProtect();

/**
 * @brief Helper function for showing the version information in console.
 *
 * This function is only a helper function and encapsulate the behavior for
 * -v or --version.
 *
 * @param name The name of the programm simply argv[0].
 * @param version The actual version of this program.
 *
 */
void showVersion(char *name, char *version);

/**
 * @brief Helper function for showing the a manual in console.
 *
 * This function is only a helper function and encapsulate the behavior for
 * -h or --help.
 *
 * @param name The name of the programm simply argv[0].
 *
 */
void showHelp(char *name);

/**
 * @brief Implements simple program opt handling
 *
 * This function checks if any opts are given at exec. if so, handles them.
 *
 * @param argc Pass from main.
 * @param argv Pass from main.
 * @param ctx Pointer to app_context struct.
 * @return 0 == all passed. 1 == need program quit. 2 == some error occurs.
 *
 * @note Pass argc and argv from the main() directly. Beware of the ctx struct.
 */
unsigned int optHandling(int argc, char **argv, struct app_context *ctx);

#endif
