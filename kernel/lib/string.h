/* string.h - String and memory manipulation functions */
#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include <stddef.h>
#include <stdint.h>

/* String length */
size_t strlen(const char *s);

/* String comparison */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* String copy */
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);

/* String duplicate (allocates memory) */
char *strdup(const char *s);

/* Find character in string */
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

/* Memory operations */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#endif /* _KERNEL_STRING_H */
