/* stdio.c - Basic I/O functions */

#include "syscall.h"

/* Standard file descriptors */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

extern size_t strlen(const char *s);

/* Write a string to stdout */
int puts(const char *s) {
    size_t len = strlen(s);
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}

/* Write a string without newline */
int fputs(const char *s, int fd) {
    return write(fd, s, strlen(s));
}

/* Put a character to stdout */
int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

/* Get a character from stdin */
int getchar(void) {
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) return -1;
    return (unsigned char)c;
}

/* Simple integer to string conversion */
static char *itoa_internal(long long val, char *buf, int base, int is_signed) {
    char *p = buf + 20;  /* Enough for 64-bit */
    int negative = 0;
    unsigned long long uval;

    *p = '\0';

    if (is_signed && val < 0) {
        negative = 1;
        uval = -val;
    } else {
        uval = val;
    }

    if (uval == 0) {
        *--p = '0';
    } else {
        const char *digits = "0123456789abcdef";
        while (uval) {
            *--p = digits[uval % base];
            uval /= base;
        }
    }

    if (negative) {
        *--p = '-';
    }

    return p;
}

/* Simple printf implementation (subset) */
int printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    int count = 0;
    char numbuf[24];
    const char *s;
    char c;
    long long n;
    unsigned long long u;

    while (*fmt) {
        if (*fmt != '%') {
            write(STDOUT_FILENO, fmt, 1);
            count++;
            fmt++;
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Handle format specifiers */
        switch (*fmt) {
        case 's':
            s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            write(STDOUT_FILENO, s, strlen(s));
            count += strlen(s);
            break;

        case 'd':
        case 'i':
            n = __builtin_va_arg(ap, int);
            s = itoa_internal(n, numbuf, 10, 1);
            write(STDOUT_FILENO, s, strlen(s));
            count += strlen(s);
            break;

        case 'u':
            u = __builtin_va_arg(ap, unsigned int);
            s = itoa_internal(u, numbuf, 10, 0);
            write(STDOUT_FILENO, s, strlen(s));
            count += strlen(s);
            break;

        case 'x':
            u = __builtin_va_arg(ap, unsigned int);
            s = itoa_internal(u, numbuf, 16, 0);
            write(STDOUT_FILENO, s, strlen(s));
            count += strlen(s);
            break;

        case 'p':
            u = (unsigned long long)__builtin_va_arg(ap, void *);
            write(STDOUT_FILENO, "0x", 2);
            s = itoa_internal(u, numbuf, 16, 0);
            write(STDOUT_FILENO, s, strlen(s));
            count += 2 + strlen(s);
            break;

        case 'c':
            c = (char)__builtin_va_arg(ap, int);
            write(STDOUT_FILENO, &c, 1);
            count++;
            break;

        case '%':
            write(STDOUT_FILENO, "%", 1);
            count++;
            break;

        case 'l':
            fmt++;
            if (*fmt == 'd' || *fmt == 'i') {
                n = __builtin_va_arg(ap, long);
                s = itoa_internal(n, numbuf, 10, 1);
                write(STDOUT_FILENO, s, strlen(s));
                count += strlen(s);
            } else if (*fmt == 'u') {
                u = __builtin_va_arg(ap, unsigned long);
                s = itoa_internal(u, numbuf, 10, 0);
                write(STDOUT_FILENO, s, strlen(s));
                count += strlen(s);
            } else if (*fmt == 'x') {
                u = __builtin_va_arg(ap, unsigned long);
                s = itoa_internal(u, numbuf, 16, 0);
                write(STDOUT_FILENO, s, strlen(s));
                count += strlen(s);
            }
            break;

        default:
            /* Unknown format, just print it */
            write(STDOUT_FILENO, "%", 1);
            write(STDOUT_FILENO, fmt, 1);
            count += 2;
            break;
        }
        fmt++;
    }

    __builtin_va_end(ap);
    return count;
}

/* Read a line from fd into buf (max size bytes)
 * Returns number of bytes read, or -1 on error/EOF */
ssize_t getline_fd(int fd, char *buf, size_t size) {
    size_t i = 0;
    char c;

    while (i < size - 1) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            if (i == 0) return -1;  /* EOF with no data */
            break;
        }

        if (c == '\n') {
            buf[i] = '\0';
            return i;
        }

        /* Handle backspace */
        if (c == '\b' || c == 0x7f) {
            if (i > 0) {
                i--;
                /* Echo backspace */
                write(fd == STDIN_FILENO ? STDOUT_FILENO : fd, "\b \b", 3);
            }
            continue;
        }

        buf[i++] = c;
    }

    buf[i] = '\0';
    return i;
}
