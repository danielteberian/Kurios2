/* stddef.h - Standard definitions */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef long unsigned int size_t;
typedef long int ptrdiff_t;

#define NULL ((void *)0)
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _STDDEF_H */
