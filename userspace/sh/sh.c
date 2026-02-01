/* sh.c - Simple shell for Kurios2 */

#include "../libc/syscall.h"

/* External libc functions */
extern size_t strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strchr(const char *s, int c);
extern void *memset(void *s, int c, size_t n);
extern int isspace(int c);
extern int printf(const char *fmt, ...);
extern ssize_t getline_fd(int fd, char *buf, size_t size);

#define MAX_LINE    256
#define MAX_ARGS    16
#define MAX_PATH    256

/* Path to search for commands */
static const char *search_paths[] = {
    "/bin/",
    "/",
    NULL
};

/* Print prompt */
static void print_prompt(void) {
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("[%s]$ ", cwd);
    } else {
        printf("$ ");
    }
}

/* Skip leading whitespace */
static char *skip_whitespace(char *s) {
    while (*s && isspace(*s)) s++;
    return s;
}

/* Parse command line into argv array
 * Returns argc (number of arguments) */
static int parse_line(char *line, char *argv[]) {
    int argc = 0;
    char *p = line;

    while (*p && argc < MAX_ARGS - 1) {
        /* Skip whitespace */
        p = skip_whitespace(p);
        if (*p == '\0') break;

        /* Start of argument */
        argv[argc++] = p;

        /* Find end of argument */
        while (*p && !isspace(*p)) p++;

        /* Null-terminate this argument */
        if (*p) {
            *p++ = '\0';
        }
    }

    argv[argc] = NULL;
    return argc;
}

/* Built-in: exit */
static int builtin_exit(int argc, char *argv[]) {
    (void)argc;
    int code = 0;
    if (argv[1]) {
        /* Parse exit code */
        code = 0;
        const char *s = argv[1];
        while (*s >= '0' && *s <= '9') {
            code = code * 10 + (*s - '0');
            s++;
        }
    }
    _exit(code);
    return 0;  /* Never reached */
}

/* Built-in: cd */
static int builtin_cd(int argc, char *argv[]) {
    (void)argc;
    const char *path = argv[1];
    if (!path) path = "/";  /* Default to root */

    if (chdir(path) < 0) {
        printf("cd: %s: No such directory\n", path);
        return 1;
    }
    return 0;
}

/* Built-in: pwd */
static int builtin_pwd(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char cwd[MAX_PATH];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
        return 0;
    } else {
        printf("pwd: error getting current directory\n");
        return 1;
    }
}

/* Built-in: echo */
static int builtin_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) write(1, " ", 1);
        write(1, argv[i], strlen(argv[i]));
    }
    write(1, "\n", 1);
    return 0;
}

/* Built-in: ls (simple version) */
static int builtin_ls(int argc, char *argv[]) {
    const char *path = ".";
    if (argc > 1) path = argv[1];

    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        printf("ls: cannot access '%s'\n", path);
        return 1;
    }

    char buf[1024];
    struct linux_dirent64 *d;
    ssize_t nread;

    while ((nread = getdents64(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t pos = 0; pos < nread; ) {
            d = (struct linux_dirent64 *)(buf + pos);

            /* Skip . and .. */
            if (strcmp(d->d_name, ".") != 0 && strcmp(d->d_name, "..") != 0) {
                if (d->d_type == DT_DIR) {
                    printf("%s/\n", d->d_name);
                } else {
                    printf("%s\n", d->d_name);
                }
            }

            pos += d->d_reclen;
        }
    }

    close(fd);
    return 0;
}

/* Built-in: cat */
static int builtin_cat(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: cat <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: No such file\n", argv[i]);
            continue;
        }

        char buf[512];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            write(1, buf, n);
        }

        close(fd);
    }
    return 0;
}

/* Built-in: mkdir */
static int builtin_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: mkdir <directory>\n");
        return 1;
    }

    if (mkdir(argv[1], 0755) < 0) {
        printf("mkdir: cannot create '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

/* Built-in: rm */
static int builtin_rm(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: rm <file>\n");
        return 1;
    }

    if (unlink(argv[1]) < 0) {
        printf("rm: cannot remove '%s'\n", argv[1]);
        return 1;
    }
    return 0;
}

/* Built-in: help */
static int builtin_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("Kurios2 Shell - Built-in commands:\n");
    printf("  cd [dir]     - Change directory\n");
    printf("  pwd          - Print working directory\n");
    printf("  ls [dir]     - List directory contents\n");
    printf("  cat <file>   - Display file contents\n");
    printf("  echo [text]  - Print text\n");
    printf("  mkdir <dir>  - Create directory\n");
    printf("  rm <file>    - Remove file\n");
    printf("  help         - Show this help\n");
    printf("  exit [code]  - Exit shell\n");
    return 0;
}

/* Check if command is a built-in, run it if so
 * Returns: 1 if built-in was run, 0 if not a built-in */
static int try_builtin(int argc, char *argv[], int *status) {
    if (argc == 0) {
        *status = 0;
        return 1;
    }

    const char *cmd = argv[0];

    if (strcmp(cmd, "exit") == 0) {
        *status = builtin_exit(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "cd") == 0) {
        *status = builtin_cd(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "pwd") == 0) {
        *status = builtin_pwd(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "echo") == 0) {
        *status = builtin_echo(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "ls") == 0) {
        *status = builtin_ls(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "cat") == 0) {
        *status = builtin_cat(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "mkdir") == 0) {
        *status = builtin_mkdir(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "rm") == 0) {
        *status = builtin_rm(argc, argv);
        return 1;
    }
    if (strcmp(cmd, "help") == 0) {
        *status = builtin_help(argc, argv);
        return 1;
    }

    return 0;  /* Not a built-in */
}

/* Try to find and execute an external command */
static int exec_external(int argc, char *argv[]) {
    (void)argc;  /* Currently unused */
    char path[MAX_PATH];
    const char *cmd = argv[0];

    /* If command contains '/', use it directly */
    if (strchr(cmd, '/')) {
        strncpy(path, cmd, MAX_PATH - 1);
        path[MAX_PATH - 1] = '\0';
    } else {
        /* Search in paths */
        int found = 0;
        for (int i = 0; search_paths[i]; i++) {
            /* Build full path */
            strncpy(path, search_paths[i], MAX_PATH - 1);
            size_t plen = strlen(path);
            strncpy(path + plen, cmd, MAX_PATH - plen - 1);
            path[MAX_PATH - 1] = '\0';

            /* Check if it exists (try to open) */
            int fd = open(path, O_RDONLY);
            if (fd >= 0) {
                close(fd);
                found = 1;
                break;
            }
        }

        if (!found) {
            printf("%s: command not found\n", cmd);
            return 127;
        }
    }

    /* Fork and exec */
    pid_t pid = fork();

    if (pid < 0) {
        printf("fork failed\n");
        return 1;
    }

    if (pid == 0) {
        /* Child process */
        execve(path, argv, NULL);
        /* If we get here, exec failed */
        printf("%s: exec failed\n", path);
        _exit(127);
    }

    /* Parent: wait for child */
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        printf("Killed by signal %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }

    return 0;
}

/* Main shell loop */
int main(int argc, char *argv[], char *envp[]) {
    (void)argc; (void)argv; (void)envp;

    char line[MAX_LINE];
    char *args[MAX_ARGS];
    int last_status = 0;

    printf("\nKurios2 Shell v0.1\n");
    printf("Type 'help' for available commands.\n\n");

    while (1) {
        print_prompt();

        /* Read a line */
        ssize_t len = getline_fd(0, line, sizeof(line));
        if (len < 0) {
            /* EOF */
            printf("\nexit\n");
            break;
        }

        /* Skip empty lines */
        if (len == 0) continue;

        /* Parse into arguments */
        int nargs = parse_line(line, args);
        if (nargs == 0) continue;

        /* Try built-in first */
        if (try_builtin(nargs, args, &last_status)) {
            continue;
        }

        /* Try external command */
        last_status = exec_external(nargs, args);
    }

    return last_status;
}
