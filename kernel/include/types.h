/* types.h - Common kernel types */
#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Physical and virtual address types */
typedef uint64_t phys_addr_t;
typedef uint64_t virt_addr_t;

/* Size types */
typedef uint64_t size_t;
typedef int64_t  ssize_t;

/* Architecture-specific types */
typedef uint64_t reg_t;         /* Register-sized value */

/* Page size constants */
#define PAGE_SIZE       4096UL
#define PAGE_SHIFT      12
#define PAGE_MASK       (~(PAGE_SIZE - 1))

/* Large page sizes */
#define PAGE_SIZE_2M    (2UL * 1024 * 1024)
#define PAGE_SIZE_1G    (1UL * 1024 * 1024 * 1024)

/* Alignment macros */
#define ALIGN_UP(x, align)   (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align) (((x) & ((align) - 1)) == 0)

/* Page alignment */
#define PAGE_ALIGN_UP(x)     ALIGN_UP(x, PAGE_SIZE)
#define PAGE_ALIGN_DOWN(x)   ALIGN_DOWN(x, PAGE_SIZE)
#define IS_PAGE_ALIGNED(x)   IS_ALIGNED(x, PAGE_SIZE)

/* Min/Max macros */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* Array size */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Bit manipulation */
#define BIT(n)           (1UL << (n))
#define SET_BIT(x, n)    ((x) |= BIT(n))
#define CLEAR_BIT(x, n)  ((x) &= ~BIT(n))
#define TEST_BIT(x, n)   (((x) >> (n)) & 1)

/* Compiler attributes */
#define PACKED          __attribute__((packed))
#define ALIGNED(n)      __attribute__((aligned(n)))
#define NORETURN        __attribute__((noreturn))
#define UNUSED          __attribute__((unused))
#define LIKELY(x)       __builtin_expect(!!(x), 1)
#define UNLIKELY(x)     __builtin_expect(!!(x), 0)

/* Memory barrier */
#define barrier()       __asm__ volatile("" ::: "memory")
#define mb()            __asm__ volatile("mfence" ::: "memory")
#define rmb()           __asm__ volatile("lfence" ::: "memory")
#define wmb()           __asm__ volatile("sfence" ::: "memory")

/* Stringify */
#define STR(x)          #x
#define XSTR(x)         STR(x)

#endif /* _KERNEL_TYPES_H */
