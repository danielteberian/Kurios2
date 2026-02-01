/* sched.c - Simple Round-Robin Scheduler */

#include "../include/types.h"
#include "sched.h"
#include "thread.h"
#include "../sync/spinlock.h"
#include "../drivers/pit.h"
#include "../debug/debug.h"
#include "../arch/x86_64/cpu.h"
#include "../smp/percpu.h"

/* Ready queue (doubly-linked list) - used when SMP is not yet initialized */
static thread_t *ready_queue_head = NULL;
static thread_t *ready_queue_tail = NULL;

/* Scheduler lock - protects the ready queue (pre-SMP) */
static spinlock_t sched_lock = SPINLOCK_INIT;

/* Scheduler running flag */
static volatile bool scheduler_running = false;

/* Time slice in ticks (10 ticks = 100ms at 100Hz) */
#define TIME_SLICE_TICKS 10

/* Current time slice remaining for the running thread (pre-SMP) */
static volatile uint32_t current_slice = 0;

/* External from thread.c */
extern void thread_set_current(thread_t *thread);
extern thread_t *thread_get_idle(void);

/*
 * Get scheduler state (per-CPU if SMP, otherwise global)
 */
static inline thread_t **get_ready_head(void) {
    if (smp_initialized()) {
        return &percpu_get()->ready_queue_head;
    }
    return &ready_queue_head;
}

static inline thread_t **get_ready_tail(void) {
    if (smp_initialized()) {
        return &percpu_get()->ready_queue_tail;
    }
    return &ready_queue_tail;
}

static inline spinlock_t *get_sched_lock(void) {
    if (smp_initialized()) {
        return &percpu_get()->sched_lock;
    }
    return &sched_lock;
}

static inline uint32_t *get_current_slice(void) {
    if (smp_initialized()) {
        return &percpu_get()->current_slice;
    }
    return (uint32_t *)&current_slice;
}

/*
 * Initialize scheduler (global state for pre-SMP boot)
 */
void sched_init(void) {
    spin_init(&sched_lock);
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    scheduler_running = false;
    current_slice = TIME_SLICE_TICKS;
}

/*
 * Initialize scheduler for a specific CPU (SMP)
 */
void sched_init_cpu(struct percpu_data *percpu) {
    if (!percpu) return;

    spin_init(&percpu->sched_lock);
    percpu->ready_queue_head = NULL;
    percpu->ready_queue_tail = NULL;
    percpu->current_slice = TIME_SLICE_TICKS;
    percpu->current_thread = NULL;
    percpu->idle_thread = NULL;

    /* Create per-CPU idle thread */
    /* Note: For now, APs share the global idle thread approach
     * A proper implementation would create per-CPU idle threads */

    DEBUG("Scheduler initialized for CPU %u", percpu->cpu_id);
}

/*
 * Add thread to ready queue (internal, must hold sched_lock)
 */
static void queue_add_locked(thread_t *thread) {
    thread_t **head = get_ready_head();
    thread_t **tail = get_ready_tail();

    thread->state = THREAD_READY;
    thread->next = NULL;
    thread->prev = *tail;

    if (*tail == NULL) {
        *head = thread;
        *tail = thread;
    } else {
        (*tail)->next = thread;
        *tail = thread;
    }
}

/*
 * Check if thread is in the ready queue
 */
static bool queue_contains_locked(thread_t *thread) {
    thread_t **head = get_ready_head();
    for (thread_t *t = *head; t; t = t->next) {
        if (t == thread) return true;
    }
    return false;
}

/*
 * Remove thread from ready queue (internal, must hold sched_lock)
 * Safe to call on threads not in queue (does nothing)
 */
static void queue_remove_locked(thread_t *thread) {
    thread_t **head = get_ready_head();
    thread_t **tail = get_ready_tail();

    /* Don't corrupt queue if thread isn't in it */
    if (!queue_contains_locked(thread)) {
        return;
    }

    if (thread->prev) {
        thread->prev->next = thread->next;
    } else {
        *head = thread->next;
    }

    if (thread->next) {
        thread->next->prev = thread->prev;
    } else {
        *tail = thread->prev;
    }

    thread->next = NULL;
    thread->prev = NULL;
}

/*
 * Add thread to ready queue (public, acquires lock)
 */
void sched_ready(thread_t *thread) {
    if (!thread || thread->state == THREAD_TERMINATED) {
        return;
    }

    spinlock_t *lock = get_sched_lock();
    uint64_t flags = spin_lock_irqsave(lock);
    queue_add_locked(thread);
    INFO("Added to ready queue: %s (TID %u)", thread->name, thread->tid);
    spin_unlock_irqrestore(lock, flags);
}

/*
 * Remove thread from ready queue (public, acquires lock)
 */
void sched_remove(thread_t *thread) {
    if (!thread) {
        return;
    }

    spinlock_t *lock = get_sched_lock();
    uint64_t flags = spin_lock_irqsave(lock);
    queue_remove_locked(thread);
    spin_unlock_irqrestore(lock, flags);
}

/*
 * Wake up sleeping threads whose wake_time has elapsed
 * Must be called with sched_lock held
 */
static void wake_sleeping_threads_locked(uint64_t now) {
    thread_t **head = get_ready_head();
    for (thread_t *t = *head; t; t = t->next) {
        if (t->state == THREAD_SLEEPING && t->wake_time <= now) {
            t->state = THREAD_READY;
        }
    }
}

/*
 * Pick next thread to run from ready queue
 * Must be called with sched_lock held
 */
static thread_t *pick_next_locked(void) {
    thread_t **head = get_ready_head();

    /* Wake sleeping threads first */
    wake_sleeping_threads_locked(pit_get_ticks());

    /* Find first ready thread */
    thread_t *next = *head;
    while (next && next->state != THREAD_READY) {
        next = next->next;
    }

    /* If no ready thread, use idle thread */
    if (!next) {
        next = thread_get_idle();
    }

    return next;
}

/*
 * Timer tick - called from PIT interrupt handler
 * This runs in interrupt context, so we must be careful with locks
 */
void sched_tick(void) {
    if (!scheduler_running) {
        return;
    }

    thread_t *current = thread_current();
    if (current) {
        current->cpu_time++;
    }

    uint32_t *slice = get_current_slice();
    spinlock_t *lock = get_sched_lock();

    /* Decrement time slice */
    if (*slice > 0) {
        (*slice)--;
    }

    /* Wake sleeping threads */
    uint64_t flags = spin_lock_irqsave(lock);
    wake_sleeping_threads_locked(pit_get_ticks());
    spin_unlock_irqrestore(lock, flags);

    /* Preempt if time slice expired and thread is still running */
    if (*slice == 0 && current && current->state == THREAD_RUNNING) {
        *slice = TIME_SLICE_TICKS;
        /* Call reschedule - will re-acquire lock internally */
        sched_reschedule();
    }
}

/*
 * Trigger a reschedule - switch to another thread
 *
 * This is the core scheduling function. It:
 * 1. Picks the next thread to run
 * 2. Updates the current thread's state
 * 3. Performs the context switch
 *
 * Note: We release the lock before context_switch because:
 * - The new thread might try to acquire it
 * - We don't want to hold a lock across a switch
 */
void sched_reschedule(void) {
    spinlock_t *lock = get_sched_lock();
    uint32_t *slice = get_current_slice();
    uint64_t flags = spin_lock_irqsave(lock);

    thread_t *current = thread_current();
    thread_t *next = pick_next_locked();

    /* No thread to run - shouldn't happen if idle thread exists */
    if (!next) {
        spin_unlock_irqrestore(lock, flags);
        return;
    }

    /* Same thread - nothing to do */
    if (next == current) {
        if (current && current->state != THREAD_RUNNING) {
            current->state = THREAD_RUNNING;
        }
        spin_unlock_irqrestore(lock, flags);
        return;
    }

    /* Handle current thread based on its state
     * Note: Running threads are NOT in the ready queue (removed when scheduled)
     * So we need to add them back when they stop running */
    if (current) {
        if (current->state == THREAD_RUNNING) {
            /* Thread was preempted - add to back of queue as READY */
            queue_add_locked(current);  /* This also sets state to READY */
        } else if (current->state == THREAD_SLEEPING) {
            /* Thread is sleeping - add to queue for wake_sleeping_threads to find */
            queue_add_locked(current);
            current->state = THREAD_SLEEPING;  /* Restore state after queue_add sets it */
        } else if (current->state == THREAD_BLOCKED) {
            /* Thread is blocked - add to queue for unblock to find */
            queue_add_locked(current);
            current->state = THREAD_BLOCKED;  /* Restore state after queue_add sets it */
        } else if (current->state == THREAD_TERMINATED) {
            /* Thread exited - don't add back to queue */
            /* Note: Thread cleanup should be done by a reaper thread */
        }
    }

    /* Remove next from ready queue and mark as running */
    queue_remove_locked(next);
    next->state = THREAD_RUNNING;
    thread_set_current(next);
    *slice = TIME_SLICE_TICKS;

    /* Release lock before context switch */
    spin_unlock_irqrestore(lock, flags);

    /* Perform context switch */
    if (current) {
        context_switch(&current->rsp, next->rsp);
    } else {
        /* First switch - bootstrap the new context */
        context_switch(&next->rsp, next->rsp);
    }
}

/*
 * Start the scheduler
 */
void sched_start(void) {
    INFO("Starting scheduler...");
    scheduler_running = true;
    uint32_t *slice = get_current_slice();
    *slice = TIME_SLICE_TICKS;
    /* Enable interrupts to allow timer ticks */
    sti();
}

/*
 * Check if scheduler is running
 */
bool sched_is_running(void) {
    return scheduler_running;
}
