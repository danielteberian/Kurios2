/* stddef.h - Standard definitions for freestanding kernel */
#ifndef _STDDEF_H
#define _STDDEF_H

typedef unsigned long long size_t;
typedef long long          ptrdiff_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _STDDEF_H */
