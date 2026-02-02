/* string.c - String and memory manipulation functions */

#include "string.h"
#include "../mm/slab.h"

size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static inline int tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + 32;
    }
    return c;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && tolower((unsigned char)*s1) == tolower((unsigned char)*s2)) {
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

char *strcpy(char *dst, const char *src) {
    char *ret = dst;
    while ((*dst++ = *src++)) {
        /* Copy including null terminator */
    }
    return ret;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *ret = dst;
    while (n > 0 && *src) {
        *dst++ = *src++;
        n--;
    }
    while (n > 0) {
        *dst++ = '\0';
        n--;
    }
    return ret;
}

char *strcat(char *dst, const char *src) {
    char *ret = dst;
    while (*dst) {
        dst++;
    }
    while ((*dst++ = *src++)) {
        /* Copy including null terminator */
    }
    return ret;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *ret = dst;
    while (*dst) {
        dst++;
    }
    while (n > 0 && *src) {
        *dst++ = *src++;
        n--;
    }
    *dst = '\0';
    return ret;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = kmalloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : (char *)last;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s) {
        /* Copy forward */
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* Copy backward */
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

/*
 * vsnprintf - Formatted string output with size limit
 *
 * Supports: %s, %d, %u, %x, %X, %lld, %llu, %llx, %c, %%
 * Width and precision specifiers are partially supported.
 */
int vsnprintf(char *str, size_t size, const char *format, __builtin_va_list ap) {
    if (size == 0) {
        return 0;
    }

    char *out = str;
    char *end = str + size - 1;  /* Leave room for null terminator */
    int written = 0;

    while (*format && out < end) {
        if (*format != '%') {
            *out++ = *format++;
            written++;
            continue;
        }

        format++;  /* Skip '%' */

        /* Handle %% */
        if (*format == '%') {
            *out++ = '%';
            format++;
            written++;
            continue;
        }

        /* Parse flags */
        int zero_pad = 0;
        int left_justify = 0;
        while (*format == '0' || *format == '-') {
            if (*format == '0') zero_pad = 1;
            if (*format == '-') left_justify = 1;
            format++;
        }

        /* Parse width */
        int width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }

        /* Parse length modifier */
        int is_long_long = 0;
        if (*format == 'l') {
            format++;
            if (*format == 'l') {
                is_long_long = 1;
                format++;
            }
        }

        /* Parse conversion specifier */
        char conv = *format++;
        char buf[32];
        char *p = buf;
        int len = 0;

        switch (conv) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && out < end) {
                *out++ = *s++;
                written++;
            }
            continue;
        }

        case 'c': {
            int c = __builtin_va_arg(ap, int);
            *out++ = (char)c;
            written++;
            continue;
        }

        case 'd':
        case 'i': {
            int64_t val;
            if (is_long_long) {
                val = __builtin_va_arg(ap, int64_t);
            } else {
                val = __builtin_va_arg(ap, int);
            }
            if (val < 0) {
                *p++ = '-';
                val = -val;
            }
            /* Convert to string (reversed) */
            char *start = p;
            do {
                *p++ = '0' + (val % 10);
                val /= 10;
            } while (val);
            len = p - buf;
            /* Reverse the digits */
            char *r = p - 1;
            while (start < r) {
                char tmp = *start;
                *start++ = *r;
                *r-- = tmp;
            }
            break;
        }

        case 'u': {
            uint64_t val;
            if (is_long_long) {
                val = __builtin_va_arg(ap, uint64_t);
            } else {
                val = __builtin_va_arg(ap, unsigned int);
            }
            char *start = p;
            do {
                *p++ = '0' + (val % 10);
                val /= 10;
            } while (val);
            len = p - buf;
            /* Reverse */
            char *r = p - 1;
            while (start < r) {
                char tmp = *start;
                *start++ = *r;
                *r-- = tmp;
            }
            break;
        }

        case 'x':
        case 'X': {
            uint64_t val;
            if (is_long_long) {
                val = __builtin_va_arg(ap, uint64_t);
            } else {
                val = __builtin_va_arg(ap, unsigned int);
            }
            const char *digits = (conv == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            char *start = p;
            do {
                *p++ = digits[val % 16];
                val /= 16;
            } while (val);
            len = p - buf;
            /* Reverse */
            char *r = p - 1;
            while (start < r) {
                char tmp = *start;
                *start++ = *r;
                *r-- = tmp;
            }
            break;
        }

        default:
            /* Unknown format, output as-is */
            *out++ = '%';
            if (out < end) *out++ = conv;
            written += 2;
            continue;
        }

        /* Output the formatted value with padding */
        int padding = width - len;

        if (!left_justify && padding > 0) {
            char pad_char = zero_pad ? '0' : ' ';
            while (padding-- > 0 && out < end) {
                *out++ = pad_char;
                written++;
            }
        }

        for (int i = 0; i < len && out < end; i++) {
            *out++ = buf[i];
            written++;
        }

        if (left_justify && padding > 0) {
            while (padding-- > 0 && out < end) {
                *out++ = ' ';
                written++;
            }
        }
    }

    *out = '\0';
    return written;
}

/*
 * snprintf - Formatted string output with size limit
 */
int snprintf(char *str, size_t size, const char *format, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    __builtin_va_end(ap);
    return ret;
}
