/* cmdline.h - Kernel Command Line Parser */
#ifndef CMDLINE_H
#define CMDLINE_H

#include "include/types.h"

/*
 * Initialize command line parser
 * cmdline: null-terminated command line string
 */
void cmdline_init(const char *cmdline);

/*
 * Get a parameter value from the command line
 * key: parameter name (e.g., "root", "init", "console")
 * Returns: parameter value string, or NULL if not found
 *
 * Examples:
 *   cmdline = "root=/dev/sda1 init=/bin/init quiet"
 *   cmdline_get_param("root") -> "/dev/sda1"
 *   cmdline_get_param("init") -> "/bin/init"
 *   cmdline_get_param("quiet") -> "" (flag present)
 *   cmdline_get_param("nope") -> NULL (not found)
 */
const char *cmdline_get_param(const char *key);

/*
 * Check if a flag is present in the command line
 * key: flag name (e.g., "quiet", "debug")
 * Returns: true if flag is present, false otherwise
 */
bool cmdline_has_flag(const char *key);

/*
 * Get the raw command line string
 * Returns: null-terminated command line, or NULL if not initialized
 */
const char *cmdline_get_raw(void);

#endif /* CMDLINE_H */
