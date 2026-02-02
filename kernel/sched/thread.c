/* thread.c - Kernel Thread Management */

#include "../include/types.h"
#include "thread.h"
#include "sched.h"
#include "../mm/slab.h"
#include "../mm/pmm.h"
#include "../drivers/pit.h"
#include "../sync/spinlock.h"
#include "../debug/debug.h"
#include "../arch/x86_64/cpu.h"
#include "../smp/percpu.h"

/* Default stack size: 16KB (4 pages) */
#define DEFAULT_STACK_SIZE  (16 * 1024)
#define STACK_ORDER         2  /* 2^2 = 4 pages */

/* Maximum threads */
#define MAX_THREADS 256

/* Thread table - maps TID to thread structure */
static thread_t *thread_table[MAX_THREADS];

/* Next TID to try allocating */
static tid_t next_tid = 1;

/* Lock protecting thread_table and next_tid */
static spinlock_t thread_lock = SPINLOCK_INIT;

/* Current running thread (global for pre-SMP, per-CPU after SMP init) */
static thread_t * volatile current_thread = NULL;

/*
 * Get/set current thread - uses per-CPU data when SMP is initialized
 */
static inline thread_t **get_current_thread_ptr(void) {
    if (smp_initialized()) {
        return &percpu_get()->current_thread;
    }
    return (thread_t **)&current_thread;
}

/* Idle thread - always exists, runs when no other threads are ready */
static thread_t *idle_thread = NULL;

/* Threading initialized flag */
static volatile bool initialized = false;

/* External trampoline from assembly */
extern void thread_entry_trampoline(void);

/*
 * Idle thread function - runs when no other threads are ready
 * It just halts waiting for interrupts (timer ticks)
 */
static void idle_thread_func(void *arg) {
    (void)arg;
    while (1) {
        hlt();  /* Wait for interrupt */
    }
}

/*
 * Allocate a thread ID (must hold thread_lock)
 * Returns 0 if no TID available (except TID 0 is reserved for boot thread)
 */
static tid_t alloc_tid_locked(void) {
    for (tid_t i = 0; i < MAX_THREADS; i++) {
        tid_t tid = (next_tid + i) % MAX_THREADS;
        if (tid == 0) {
            tid = 1;  /* Skip TID 0 - reserved for boot thread */
        }
        if (thread_table[tid] == NULL) {
            next_tid = (tid + 1) % MAX_THREADS;
            if (next_tid == 0) next_tid = 1;
            return tid;
        }
    }
    return 0;  /* No free TID - table is full */
}

/*
 * Copy thread name safely
 */
static void copy_thread_name(thread_t *thread, const char *name) {
    if (!name) {
        thread->name[0] = '\0';
        return;
    }
    int i;
    for (i = 0; i < 31 && name[i]; i++) {
        thread->name[i] = name[i];
    }
    thread->name[i] = '\0';
}

/*
 * Initialize threading subsystem
 */
void thread_init(void) {
    INFO("Initializing threading subsystem...");

    spin_init(&thread_lock);

    /* Clear thread table */
    for (int i = 0; i < MAX_THREADS; i++) {
        thread_table[i] = NULL;
    }

    /* Create thread 0 for the current (boot) context
     * This represents the thread that's been running since boot */
    thread_t *boot_thread = kmalloc(sizeof(thread_t));
    if (!boot_thread) {
        panic("Failed to allocate boot thread");
    }

    boot_thread->tid = 0;
    boot_thread->state = THREAD_RUNNING;
    boot_thread->stack_base = NULL;  /* Boot stack - managed by bootloader */
    boot_thread->stack_size = 0;
    boot_thread->rsp = 0;  /* Will be set on first context switch */
    boot_thread->priority = 10;
    boot_thread->cpu_time = 0;
    boot_thread->wake_time = 0;
    boot_thread->next = NULL;
    boot_thread->prev = NULL;
    copy_thread_name(boot_thread, "boot");

    thread_table[0] = boot_thread;
    *get_current_thread_ptr() = boot_thread;

    /* Initialize scheduler before creating threads */
    sched_init();

    /* Create idle thread - it should always exist
     * Note: thread_create adds it to the ready queue, but idle is special -
     * it should only run when no other threads are ready, so remove it */
    idle_thread = thread_create("idle", idle_thread_func, NULL);
    if (!idle_thread) {
        panic("Failed to create idle thread");
    }
    idle_thread->priority = 255;  /* Lowest priority */

    /* Remove idle from ready queue - it's only used when no other threads ready */
    sched_remove(idle_thread);
    idle_thread->state = THREAD_READY;  /* Keep it ready but not in queue */

    initialized = true;
    INFO("Threading initialized: boot thread TID 0, idle thread TID %u",
         idle_thread->tid);
}

/*
 * Create a new kernel thread
 *
 * @param name  Thread name (for debugging)
 * @param entry Thread entry function
 * @param arg   Argument passed to entry function
 * @return New thread pointer, or NULL on failure
 */
thread_t *thread_create(const char *name, thread_entry_t entry, void *arg) {
    if (!entry) {
        ERROR("thread_create: NULL entry function");
        return NULL;
    }

    uint64_t flags = spin_lock_irqsave(&thread_lock);

    /* Allocate thread structure from slab allocator */
    thread_t *thread = kmalloc(sizeof(thread_t));
    if (!thread) {
        spin_unlock_irqrestore(&thread_lock, flags);
        ERROR("Failed to allocate thread structure");
        return NULL;
    }

    /* Allocate TID */
    tid_t tid = alloc_tid_locked();
    if (tid == 0 && thread_table[0] != NULL) {
        kfree(thread);
        spin_unlock_irqrestore(&thread_lock, flags);
        ERROR("No free thread IDs (max %d threads)", MAX_THREADS);
        return NULL;
    }

    /* Allocate stack from kernel heap (properly mapped virtual memory)
     * Using kmalloc ensures the stack is in the kernel's virtual address space */
    void *stack_base = kmalloc(DEFAULT_STACK_SIZE);
    if (!stack_base) {
        kfree(thread);
        spin_unlock_irqrestore(&thread_lock, flags);
        ERROR("Failed to allocate thread stack");
        return NULL;
    }

    /* Initialize thread structure */
    thread->tid = tid;
    thread->state = THREAD_READY;
    thread->stack_base = stack_base;
    thread->stack_size = DEFAULT_STACK_SIZE;
    thread->priority = 10;  /* Default priority */
    thread->cpu_time = 0;
    thread->wake_time = 0;
    thread->next = NULL;
    thread->prev = NULL;
    copy_thread_name(thread, name);

    /* Set up initial stack frame for context switch
     *
     * Stack layout (growing downward):
     *   [stack_top]     <- entry function (for trampoline)
     *   [stack_top - 8] <- arg (for trampoline)
     *   [stack_top - 16] <- return address (thread_entry_trampoline)
     *   [stack_top - 24] <- rflags
     *   [stack_top - 32] <- r15
     *   [stack_top - 40] <- r14
     *   [stack_top - 48] <- r13
     *   [stack_top - 56] <- r12
     *   [stack_top - 64] <- rbx
     *   [stack_top - 72] <- rbp <- thread->rsp points here
     *
     * When context_switch restores this, it will:
     *   1. Pop callee-saved registers
     *   2. ret to thread_entry_trampoline
     *   3. Trampoline pops arg into rdi, entry into rax, calls rax(rdi)
     */
    uint64_t *stack_top = (uint64_t *)((uint8_t *)stack_base + DEFAULT_STACK_SIZE);

    /* Values for thread_entry_trampoline */
    *(--stack_top) = (uint64_t)entry;   /* Entry function */
    *(--stack_top) = (uint64_t)arg;     /* Argument */

    /* Return address for context_switch's ret instruction */
    *(--stack_top) = (uint64_t)thread_entry_trampoline;

    /* Callee-saved registers (will be popped by context_switch) */
    *(--stack_top) = 0x202;  /* rflags: IF=1 (interrupts enabled) */
    *(--stack_top) = 0;      /* r15 */
    *(--stack_top) = 0;      /* r14 */
    *(--stack_top) = 0;      /* r13 */
    *(--stack_top) = 0;      /* r12 */
    *(--stack_top) = 0;      /* rbx */
    *(--stack_top) = 0;      /* rbp */

    thread->rsp = (uint64_t)stack_top;

    /* Add to thread table */
    thread_table[tid] = thread;

    spin_unlock_irqrestore(&thread_lock, flags);

    /* Add to scheduler's ready queue
     * Note: We do this after releasing thread_lock to avoid holding
     * two locks simultaneously (lock ordering: thread_lock before sched_lock) */
    sched_ready(thread);

    DEBUG("Created thread '%s' TID %u, stack 0x%llx-0x%llx",
          name, tid, (uint64_t)stack_base,
          (uint64_t)stack_base + DEFAULT_STACK_SIZE);

    return thread;
}

/*
 * Get current thread
 */
thread_t *thread_current(void) {
    return *get_current_thread_ptr();
}

/*
 * Set current thread (called by scheduler during context switch)
 */
void thread_set_current(thread_t *thread) {
    *get_current_thread_ptr() = thread;
}

/*
 * Yield CPU to another thread voluntarily
 */
void thread_yield(void) {
    sched_reschedule();
}

/*
 * Exit current thread
 * This function does not return
 */
void thread_exit(void) {
    thread_t *thread = thread_current();

    cli();  /* Disable interrupts during state change */

    thread->state = THREAD_TERMINATED;

    DEBUG("Thread '%s' TID %u exiting", thread->name, thread->tid);

    /* Switch to another thread - we won't return */
    sched_reschedule();

    /* Should never reach here */
    panic("thread_exit: returned from reschedule");
}

/*
 * Sleep current thread for specified milliseconds
 */
void thread_sleep_ms(uint32_t ms) {
    if (ms == 0) {
        return;
    }

    uint64_t freq = pit_get_frequency();
    if (freq == 0) {
        WARN("thread_sleep_ms: timer not initialized");
        return;
    }

    cli();  /* Disable interrupts during state change */

    thread_t *current = thread_current();
    current->state = THREAD_SLEEPING;
    current->wake_time = pit_get_ticks() + (ms * freq) / 1000;

    sched_reschedule();
    /* Returns after we wake up */
}

/*
 * Block current thread (wait for an event)
 */
void thread_block(void) {
    cli();  /* Disable interrupts during state change */

    thread_current()->state = THREAD_BLOCKED;

    sched_reschedule();
    /* Returns after we're unblocked */
}

/*
 * Unblock a waiting thread
 */
void thread_unblock(thread_t *thread) {
    if (!thread) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&thread_lock);

    if (thread->state == THREAD_BLOCKED) {
        thread->state = THREAD_READY;
        spin_unlock_irqrestore(&thread_lock, flags);
        /* Add to scheduler queue after releasing lock */
        sched_ready(thread);
    } else {
        spin_unlock_irqrestore(&thread_lock, flags);
    }
}

/*
 * Get thread by ID
 */
thread_t *thread_get(tid_t tid) {
    if (tid >= MAX_THREADS) {
        return NULL;
    }
    /* No lock needed - read of pointer is atomic */
    return thread_table[tid];
}

/*
 * Check if threading is initialized
 */
bool thread_is_initialized(void) {
    return initialized;
}

/*
 * Get idle thread (used by scheduler when no other threads are ready)
 */
thread_t *thread_get_idle(void) {
    return idle_thread;
}
