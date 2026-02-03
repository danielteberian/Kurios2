/* cmdline.c - Kernel Command Line Parser Implementation */

#include "cmdline.h"
#include "lib/string.h"
#include "debug/debug.h"
#include "mm/slab.h"

/* Maximum command line length */
#define CMDLINE_MAX_LEN 1024

/* Parsed command line storage */
static char cmdline_buffer[CMDLINE_MAX_LEN];
static bool cmdline_initialized = false;

/*
 * Initialize command line parser
 */
void cmdline_init(const char *cmdline)
{
    if (!cmdline || *cmdline == '\0') {
        cmdline_buffer[0] = '\0';
        cmdline_initialized = true;
        DEBUG("Command line: (empty)");
        return;
    }

    /* Copy command line to buffer */
    size_t len = strlen(cmdline);
    if (len >= CMDLINE_MAX_LEN) {
        WARN("Command line too long (%zu bytes), truncating to %d",
             len, CMDLINE_MAX_LEN - 1);
        len = CMDLINE_MAX_LEN - 1;
    }

    memcpy(cmdline_buffer, cmdline, len);
    cmdline_buffer[len] = '\0';
    cmdline_initialized = true;

    INFO("Command line: %s", cmdline_buffer);
}

/*
 * Get a parameter value from the command line
 */
const char *cmdline_get_param(const char *key)
{
    if (!cmdline_initialized || !key) {
        return NULL;
    }

    size_t key_len = strlen(key);
    char *p = cmdline_buffer;

    while (*p) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        /* Check if this token matches our key */
        if (strncmp(p, key, key_len) == 0) {
            /* Check if followed by '=' or whitespace/end */
            if (p[key_len] == '=') {
                /* key=value format */
                return p + key_len + 1;
            } else if (p[key_len] == ' ' || p[key_len] == '\t' || p[key_len] == '\0') {
                /* key (flag) format - return empty string */
                static char empty[] = "";
                return empty;
            }
        }

        /* Skip to next token */
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }

    return NULL;
}

/*
 * Check if a flag is present
 */
bool cmdline_has_flag(const char *key)
{
    const char *value = cmdline_get_param(key);
    return value != NULL;
}

/*
 * Get the raw command line
 */
const char *cmdline_get_raw(void)
{
    if (!cmdline_initialized) {
        return NULL;
    }
    return cmdline_buffer;
}
