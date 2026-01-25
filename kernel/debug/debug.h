/* debug.h - Kernel debugging and logging framework */
#ifndef _KERNEL_DEBUG_H
#define _KERNEL_DEBUG_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../include/types.h"

/* Log levels */
typedef enum {
    LOG_TRACE = 0,      /* Detailed trace information */
    LOG_DEBUG = 1,      /* Debug information */
    LOG_INFO  = 2,      /* General information */
    LOG_WARN  = 3,      /* Warnings */
    LOG_ERROR = 4,      /* Errors */
    LOG_FATAL = 5,      /* Fatal errors (will panic) */
    LOG_NONE  = 6       /* Disable logging */
} log_level_t;

/* Current log level (messages below this level are suppressed) */
extern log_level_t g_log_level;

/* Initialize debug subsystem */
void debug_init(void);

/* Set log level */
void debug_set_level(log_level_t level);

/* Core printf-style output */
int kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int kvprintf(const char *fmt, va_list args);

/* Logging macros with automatic file/line info */
#define LOG(level, fmt, ...) \
    log_write(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define TRACE(fmt, ...)  LOG(LOG_TRACE, fmt, ##__VA_ARGS__)
#define DEBUG(fmt, ...)  LOG(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define INFO(fmt, ...)   LOG(LOG_INFO, fmt, ##__VA_ARGS__)
#define WARN(fmt, ...)   LOG(LOG_WARN, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...)  LOG(LOG_ERROR, fmt, ##__VA_ARGS__)
#define FATAL(fmt, ...)  LOG(LOG_FATAL, fmt, ##__VA_ARGS__)

/* Log write function */
void log_write(log_level_t level, const char *file, int line,
               const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/* Panic - unrecoverable error */
void panic(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

/* Panic with CPU state dump */
void panic_with_state(const char *msg, void *cpu_state) __attribute__((noreturn));

/* Assertions */
#ifdef NDEBUG
    #define ASSERT(expr)          ((void)0)
    #define ASSERT_MSG(expr, msg) ((void)0)
#else
    #define ASSERT(expr) \
        do { \
            if (UNLIKELY(!(expr))) { \
                assertion_failed(#expr, __FILE__, __LINE__, __func__); \
            } \
        } while (0)

    #define ASSERT_MSG(expr, msg) \
        do { \
            if (UNLIKELY(!(expr))) { \
                assertion_failed_msg(#expr, msg, __FILE__, __LINE__, __func__); \
            } \
        } while (0)
#endif

/* Assertion failure handlers */
void assertion_failed(const char *expr, const char *file,
                      int line, const char *func) __attribute__((noreturn));
void assertion_failed_msg(const char *expr, const char *msg,
                          const char *file, int line,
                          const char *func) __attribute__((noreturn));

/* Static assertions (compile-time) */
#define STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)

/* Debug breakpoint (triggers debug exception if debugger attached) */
static inline void breakpoint(void) {
    __asm__ volatile("int3");
}

/* Hex dump utility */
void hex_dump(const void *data, size_t len, uint64_t base_addr);

/* Stack trace */
void stack_trace(void);
void stack_trace_from(uint64_t rbp, uint64_t rip);

/* Register dump */
void dump_registers(void *cpu_state);

/* Debug output control */
void debug_enable_serial(bool enable);
void debug_enable_vga(bool enable);

#endif /* _KERNEL_DEBUG_H */
