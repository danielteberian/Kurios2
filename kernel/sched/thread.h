/* thread.h - Kernel Thread Management */
#ifndef _SCHED_THREAD_H
#define _SCHED_THREAD_H

#include <stdint.h>
#include <stdbool.h>

/* Thread states */
typedef enum {
    THREAD_READY,       /* Ready to run */
    THREAD_RUNNING,     /* Currently executing */
    THREAD_BLOCKED,     /* Waiting for something */
    THREAD_SLEEPING,    /* Sleeping until wake time */
    THREAD_TERMINATED   /* Finished execution */
} thread_state_t;

/* Thread ID type */
typedef uint32_t tid_t;

/* Thread entry function */
typedef void (*thread_entry_t)(void *arg);

/* CPU context saved during context switch */
typedef struct {
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rbx;
    uint64_t rdi, rsi;
    uint64_t rdx, rcx, rax;
    uint64_t rflags;
    uint64_t rip;
    uint64_t rsp;
} __attribute__((packed)) thread_context_t;

/* Thread Control Block */
typedef struct thread {
    tid_t tid;                      /* Thread ID */
    char name[32];                  /* Thread name */
    thread_state_t state;           /* Current state */

    /* Stack */
    void *stack_base;               /* Stack allocation base */
    uint64_t stack_size;            /* Stack size */
    uint64_t rsp;                   /* Current stack pointer */

    /* Scheduling */
    uint64_t wake_time;             /* Wake time for sleeping threads */
    uint32_t priority;              /* Priority (lower = higher priority) */
    uint64_t cpu_time;              /* CPU time used (ticks) */
    uint32_t cpu_mask;              /* CPU affinity mask (bit N = allowed on CPU N) */

    /* Linked list for scheduler queues */
    struct thread *next;
    struct thread *prev;
} thread_t;

/* Initialize threading subsystem */
void thread_init(void);

/* Create a new kernel thread */
thread_t *thread_create(const char *name, thread_entry_t entry, void *arg);

/* Get current thread */
thread_t *thread_current(void);

/* Yield CPU to another thread */
void thread_yield(void);

/* Exit current thread */
void thread_exit(void);

/* Sleep for milliseconds */
void thread_sleep_ms(uint32_t ms);

/* Block current thread */
void thread_block(void);

/* Unblock a thread */
void thread_unblock(thread_t *thread);

/* Get thread by ID */
thread_t *thread_get(tid_t tid);

/* Check if threading is initialized */
bool thread_is_initialized(void);

/* Create per-CPU idle thread (for SMP) */
thread_t *thread_create_idle(uint32_t cpu_id);

#endif /* _SCHED_THREAD_H */
