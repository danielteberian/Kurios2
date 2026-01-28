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

/* Runtime support (defined in entry.asm) */
extern void call_global_destructors(void);

/*
 * Error numbers (subset of POSIX errno values)
 */
#define EPERM           1       /* Operation not permitted */
#define ENOENT          2       /* No such file or directory */
#define ESRCH           3       /* No such process */
#define EINTR           4       /* Interrupted system call */
#define EIO             5       /* I/O error */
#define ENXIO           6       /* No such device or address */
#define E2BIG           7       /* Argument list too long */
#define ENOEXEC         8       /* Exec format error */
#define EBADF           9       /* Bad file number */
#define ECHILD          10      /* No child processes */
#define EAGAIN          11      /* Try again */
#define ENOMEM          12      /* Out of memory */
#define EACCES          13      /* Permission denied */
#define EFAULT          14      /* Bad address */
#define EBUSY           16      /* Device or resource busy */
#define EEXIST          17      /* File exists */
#define EXDEV           18      /* Cross-device link */
#define ENODEV          19      /* No such device */
#define ENOTDIR         20      /* Not a directory */
#define EISDIR          21      /* Is a directory */
#define EINVAL          22      /* Invalid argument */
#define ENFILE          23      /* File table overflow */
#define EMFILE          24      /* Too many open files */
#define ENOTTY          25      /* Not a typewriter */
#define EFBIG           27      /* File too large */
#define ENOSPC          28      /* No space left on device */
#define ESPIPE          29      /* Illegal seek */
#define EROFS           30      /* Read-only file system */
#define EMLINK          31      /* Too many links */
#define EPIPE           32      /* Broken pipe */
#define EDOM            33      /* Math argument out of domain */
#define ERANGE          34      /* Math result not representable */
#define ENOSYS          38      /* Function not implemented */
#define ENOTEMPTY       39      /* Directory not empty */
#define ELOOP           40      /* Too many symbolic links */
#define EWOULDBLOCK     EAGAIN  /* Operation would block */
#define ENAMETOOLONG    36      /* File name too long */

#endif /* _KERNEL_TYPES_H */
