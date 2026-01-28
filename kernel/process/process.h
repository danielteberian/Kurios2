/* process.h - Process Management */
#ifndef _PROCESS_PROCESS_H
#define _PROCESS_PROCESS_H

#include <stdint.h>
#include <stdbool.h>
#include "../sched/thread.h"

/* Forward declaration */
struct fd_table;

/* Process ID type */
typedef uint32_t pid_t;

/* Invalid PID constant */
#define PID_INVALID     ((pid_t)-1)

/* Maximum number of processes */
#define MAX_PROCESSES   256

/* Process states */
typedef enum {
    PROC_UNUSED,        /* Slot is free */
    PROC_EMBRYO,        /* Being created */
    PROC_READY,         /* Ready to run */
    PROC_RUNNING,       /* Currently executing */
    PROC_BLOCKED,       /* Waiting for something */
    PROC_ZOMBIE,        /* Exited, waiting for parent to reap */
    PROC_DEAD           /* Fully terminated, slot can be reused */
} process_state_t;

/* Forward declaration */
struct process;

/*
 * Process structure
 *
 * This represents a single process in the system. Each process has its own
 * address space (page tables) and can contain one or more threads.
 */
typedef struct process {
    /* Identity */
    pid_t pid;                      /* Process ID */
    char name[32];                  /* Process name */
    process_state_t state;          /* Current state */

    /* Address space */
    uint64_t cr3;                   /* Page table root (physical address) */

    /* Kernel stack for this process (used during syscalls/interrupts) */
    void *kernel_stack;             /* Base of kernel stack allocation */
    uint64_t kernel_stack_size;     /* Size of kernel stack */
    uint64_t kernel_rsp;            /* Current kernel stack pointer */

    /* Process relationships */
    pid_t parent_pid;               /* Parent process ID */
    int exit_code;                  /* Exit status (valid when ZOMBIE) */

    /* Main thread (for single-threaded processes) */
    thread_t *main_thread;          /* Primary thread of execution */

    /* Statistics */
    uint64_t start_time;            /* Time when process was created (ticks) */
    uint64_t cpu_time;              /* Total CPU time used (ticks) */

    /* File descriptors */
    struct fd_table *fd_table;      /* Per-process file descriptor table */

    /* User-space entry (for new processes) */
    uint64_t entry_point;           /* User-space entry address */
    uint64_t user_stack;            /* User-space stack pointer */
} process_t;

/*
 * Initialize process management subsystem
 * Must be called after threading is initialized
 */
void process_init(void);

/*
 * Create a new kernel process (no user-space mapping)
 * This is mainly for testing the process infrastructure
 *
 * @param name  Process name
 * @return New process, or NULL on failure
 */
process_t *process_create(const char *name);

/*
 * Destroy a process and free all resources
 * The process must be in ZOMBIE or DEAD state
 *
 * @param proc  Process to destroy
 */
void process_destroy(process_t *proc);

/*
 * Get process by PID
 *
 * @param pid   Process ID to look up
 * @return Process pointer, or NULL if not found
 */
process_t *process_get_by_pid(pid_t pid);

/*
 * Get current process (process of the current thread)
 *
 * @return Current process, or NULL if none
 */
process_t *process_current(void);

/*
 * Set the current process (called during context switch)
 *
 * @param proc  Process to set as current
 */
void process_set_current(process_t *proc);

/*
 * Mark a process as exited
 *
 * @param proc      Process to exit
 * @param exit_code Exit status code
 */
void process_exit(process_t *proc, int exit_code);

/*
 * Get process state as string (for debugging)
 *
 * @param state Process state
 * @return State name string
 */
const char *process_state_name(process_state_t state);

/*
 * Dump all processes (for debugging)
 */
void process_dump_all(void);

/*
 * Check if process subsystem is initialized
 */
bool process_is_initialized(void);

/*
 * Get total number of active processes
 */
uint32_t process_count(void);

#ifdef DEBUG_TESTS
/*
 * Run process subsystem tests
 */
void process_run_tests(void);
#endif

#endif /* _PROCESS_PROCESS_H */
