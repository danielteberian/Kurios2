/* sched.c - Simple Round-Robin Scheduler */

#include "../include/types.h"
#include "sched.h"
#include "thread.h"
#include "../sync/spinlock.h"
#include "../drivers/pit.h"
#include "../debug/debug.h"
#include "../arch/x86_64/cpu.h"

/* Ready queue (doubly-linked list) */
static thread_t *ready_queue_head = NULL;
static thread_t *ready_queue_tail = NULL;

/* Scheduler lock - protects the ready queue */
static spinlock_t sched_lock = SPINLOCK_INIT;

/* Scheduler running flag */
static volatile bool scheduler_running = false;

/* Time slice in ticks (10 ticks = 100ms at 100Hz) */
#define TIME_SLICE_TICKS 10

/* Current time slice remaining for the running thread */
static volatile uint32_t current_slice = 0;

/* External from thread.c */
extern void thread_set_current(thread_t *thread);
extern thread_t *thread_get_idle(void);

/*
 * Initialize scheduler
 */
void sched_init(void) {
    spin_init(&sched_lock);
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
    scheduler_running = false;
    current_slice = TIME_SLICE_TICKS;
}

/*
 * Add thread to ready queue (internal, must hold sched_lock)
 */
static void queue_add_locked(thread_t *thread) {
    thread->state = THREAD_READY;
    thread->next = NULL;
    thread->prev = ready_queue_tail;

    if (ready_queue_tail == NULL) {
        ready_queue_head = thread;
        ready_queue_tail = thread;
    } else {
        ready_queue_tail->next = thread;
        ready_queue_tail = thread;
    }
}

/*
 * Check if thread is in the ready queue
 */
static bool queue_contains_locked(thread_t *thread) {
    for (thread_t *t = ready_queue_head; t; t = t->next) {
        if (t == thread) return true;
    }
    return false;
}

/*
 * Remove thread from ready queue (internal, must hold sched_lock)
 * Safe to call on threads not in queue (does nothing)
 */
static void queue_remove_locked(thread_t *thread) {
    /* Don't corrupt queue if thread isn't in it */
    if (!queue_contains_locked(thread)) {
        return;
    }

    if (thread->prev) {
        thread->prev->next = thread->next;
    } else {
        ready_queue_head = thread->next;
    }

    if (thread->next) {
        thread->next->prev = thread->prev;
    } else {
        ready_queue_tail = thread->prev;
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

    uint64_t flags = spin_lock_irqsave(&sched_lock);
    queue_add_locked(thread);
    INFO("Added to ready queue: %s (TID %u)", thread->name, thread->tid);
    spin_unlock_irqrestore(&sched_lock, flags);
}

/*
 * Remove thread from ready queue (public, acquires lock)
 */
void sched_remove(thread_t *thread) {
    if (!thread) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&sched_lock);
    queue_remove_locked(thread);
    spin_unlock_irqrestore(&sched_lock, flags);
}

/*
 * Wake up sleeping threads whose wake_time has elapsed
 * Must be called with sched_lock held
 */
static void wake_sleeping_threads_locked(uint64_t now) {
    for (thread_t *t = ready_queue_head; t; t = t->next) {
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
    /* Wake sleeping threads first */
    wake_sleeping_threads_locked(pit_get_ticks());

    /* Find first ready thread */
    thread_t *next = ready_queue_head;
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

    /* Decrement time slice */
    if (current_slice > 0) {
        current_slice--;
    }

    /* Wake sleeping threads */
    uint64_t flags = spin_lock_irqsave(&sched_lock);
    wake_sleeping_threads_locked(pit_get_ticks());
    spin_unlock_irqrestore(&sched_lock, flags);

    /* Preempt if time slice expired and thread is still running */
    if (current_slice == 0 && current && current->state == THREAD_RUNNING) {
        current_slice = TIME_SLICE_TICKS;
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
    uint64_t flags = spin_lock_irqsave(&sched_lock);

    thread_t *current = thread_current();
    thread_t *next = pick_next_locked();

    /* No thread to run - shouldn't happen if idle thread exists */
    if (!next) {
        spin_unlock_irqrestore(&sched_lock, flags);
        return;
    }

    /* Same thread - nothing to do */
    if (next == current) {
        if (current && current->state != THREAD_RUNNING) {
            current->state = THREAD_RUNNING;
        }
        spin_unlock_irqrestore(&sched_lock, flags);
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
    current_slice = TIME_SLICE_TICKS;

    /* Release lock before context switch */
    spin_unlock_irqrestore(&sched_lock, flags);

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
    current_slice = TIME_SLICE_TICKS;
    /* Enable interrupts to allow timer ticks */
    sti();
}

/*
 * Check if scheduler is running
 */
bool sched_is_running(void) {
    return scheduler_running;
}
