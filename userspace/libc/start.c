/* start.c - C runtime startup */

#include "syscall.h"

/* Main function (provided by the application) */
extern int main(int argc, char *argv[], char *envp[]);

/*
 * Entry point from ELF loader
 *
 * Stack layout when we get control:
 *   [RSP+0]  = argc
 *   [RSP+8]  = argv[0]
 *   [RSP+16] = argv[1]
 *   ...
 *   [RSP+?]  = NULL (end of argv)
 *   [RSP+?]  = envp[0]
 *   ...
 *   [RSP+?]  = NULL (end of envp)
 *
 * For now, the kernel sets up a minimal stack.
 */
void _start(void) {
    /* For now, just call main with no arguments
     * TODO: Parse argc/argv from stack */
    int argc = 0;
    char **argv = (char **)0;
    char **envp = (char **)0;

    int ret = main(argc, argv, envp);

    _exit(ret);
}
