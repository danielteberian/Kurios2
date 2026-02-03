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

    /* Create per-CPU idle thread
     * Each CPU gets its own idle thread to avoid race conditions */
    extern thread_t *thread_create_idle(uint32_t cpu_id);
    thread_t *idle = thread_create_idle(percpu->cpu_id);
    if (!idle) {
        ERROR("Failed to create idle thread for CPU %u", percpu->cpu_id);
        percpu->idle_thread = NULL;
        return;
    }

    percpu->idle_thread = idle;

    DEBUG("Scheduler initialized for CPU %u (idle thread TID %u)",
          percpu->cpu_id, idle->tid);
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
    uint32_t current_cpu = smp_initialized() ? cpu_id() : 0;
    uint32_t cpu_bit = 1 << current_cpu;

    /* Wake sleeping threads first */
    wake_sleeping_threads_locked(pit_get_ticks());

    /* Find first ready thread that's allowed on this CPU */
    thread_t *next = *head;
    while (next) {
        if (next->state == THREAD_READY && (next->cpu_mask & cpu_bit)) {
            break;  /* Found a ready thread allowed on this CPU */
        }
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
    static uint64_t balance_tick = 0;

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

    /* Load balancing - every 100 ticks (1 second at 100Hz) */
    balance_tick++;
    if (balance_tick >= 100) {
        balance_tick = 0;
        sched_balance_load();
    }

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

/*
 * Get the number of threads in a CPU's ready queue
 * cpu_id = 0 means current CPU
 */
uint32_t sched_queue_length(uint32_t cpu_id) {
    if (!smp_initialized()) {
        /* Single CPU - count global queue */
        uint32_t count = 0;
        for (thread_t *t = ready_queue_head; t; t = t->next) {
            if (t->state == THREAD_READY) count++;
        }
        return count;
    }

    /* Multi-CPU - count per-CPU queue */
    percpu_data_t *percpu;
    if (cpu_id == 0) {
        percpu = percpu_get();
    } else {
        percpu = percpu_get_cpu(cpu_id);
    }

    if (!percpu) return 0;

    uint32_t count = 0;
    uint64_t flags = spin_lock_irqsave(&percpu->sched_lock);
    for (thread_t *t = percpu->ready_queue_head; t; t = t->next) {
        if (t->state == THREAD_READY) count++;
    }
    spin_unlock_irqrestore(&percpu->sched_lock, flags);

    return count;
}

/*
 * Load balancing - migrate threads from busy CPUs to idle CPUs
 * Called periodically from timer interrupt
 */
void sched_balance_load(void) {
    if (!smp_initialized()) {
        return;  /* No balancing needed on single CPU */
    }

    uint32_t num_cpus = cpu_count();
    if (num_cpus <= 1) {
        return;
    }

    /* Simple load balancing strategy:
     * Find the CPU with the most threads and the CPU with the least threads.
     * If the difference is > 2, migrate one thread from busy to idle CPU.
     */

    uint32_t max_load = 0;
    uint32_t min_load = 0xFFFFFFFF;
    uint32_t busiest_cpu = 0;
    uint32_t idlest_cpu = 0;

    /* Find busiest and idlest CPUs */
    for (uint32_t i = 0; i < num_cpus; i++) {
        uint32_t load = sched_queue_length(i);
        if (load > max_load) {
            max_load = load;
            busiest_cpu = i;
        }
        if (load < min_load) {
            min_load = load;
            idlest_cpu = i;
        }
    }

    /* Only balance if difference is significant (> 2 threads) */
    if (max_load - min_load <= 2) {
        return;
    }

    /* Don't migrate if busiest is current CPU (avoid complications) */
    if (busiest_cpu == cpu_id()) {
        return;
    }

    /* Migrate one thread from busiest to idlest */
    percpu_data_t *busy_percpu = percpu_get_cpu(busiest_cpu);
    percpu_data_t *idle_percpu = percpu_get_cpu(idlest_cpu);

    if (!busy_percpu || !idle_percpu) {
        return;
    }

    /* Lock both queues (always lock lower ID first to avoid deadlock) */
    percpu_data_t *first = (busiest_cpu < idlest_cpu) ? busy_percpu : idle_percpu;
    percpu_data_t *second = (busiest_cpu < idlest_cpu) ? idle_percpu : busy_percpu;

    uint64_t flags1 = spin_lock_irqsave(&first->sched_lock);
    uint64_t flags2 = spin_lock_irqsave(&second->sched_lock);

    /* Find a READY thread on the busy CPU that's allowed on the idle CPU */
    uint32_t idle_cpu_bit = 1 << idlest_cpu;
    thread_t *thread = busy_percpu->ready_queue_head;
    while (thread) {
        if (thread->state == THREAD_READY && (thread->cpu_mask & idle_cpu_bit)) {
            break;  /* Found thread allowed on target CPU */
        }
        thread = thread->next;
    }

    if (thread) {
        /* Remove from busy CPU's queue */
        if (thread->prev) {
            thread->prev->next = thread->next;
        } else {
            busy_percpu->ready_queue_head = thread->next;
        }
        if (thread->next) {
            thread->next->prev = thread->prev;
        } else {
            busy_percpu->ready_queue_tail = thread->prev;
        }

        /* Add to idle CPU's queue */
        thread->next = NULL;
        thread->prev = idle_percpu->ready_queue_tail;
        if (idle_percpu->ready_queue_tail) {
            idle_percpu->ready_queue_tail->next = thread;
        } else {
            idle_percpu->ready_queue_head = thread;
        }
        idle_percpu->ready_queue_tail = thread;

        DEBUG("Load balance: migrated thread %s (TID %u) from CPU %u to CPU %u",
              thread->name, thread->tid, busiest_cpu, idlest_cpu);
    }

    spin_unlock_irqrestore(&second->sched_lock, flags2);
    spin_unlock_irqrestore(&first->sched_lock, flags1);
}

/*
 * Set CPU affinity for a thread
 * cpu_mask is a bitmask where bit N means thread can run on CPU N
 * Returns 0 on success, -EINVAL on invalid parameters, -ESRCH if thread not found
 */
int sched_setaffinity(tid_t tid, uint32_t cpu_mask) {
    if (cpu_mask == 0) {
        return -22;  /* -EINVAL: must allow at least one CPU */
    }

    extern thread_t *thread_get(tid_t tid);
    thread_t *thread = thread_get(tid);
    if (!thread) {
        return -3;  /* -ESRCH */
    }

    thread->cpu_mask = cpu_mask;

    /* If thread is currently on a CPU it's not allowed on, trigger reschedule */
    if (thread->state == THREAD_RUNNING) {
        if (smp_initialized()) {
            uint32_t current_cpu = cpu_id();
            if (!(cpu_mask & (1 << current_cpu))) {
                /* Thread not allowed on current CPU - need to migrate */
                sched_reschedule();
            }
        }
    }

    return 0;
}

/*
 * Get CPU affinity for a thread
 * Returns 0 on success, -ESRCH if thread not found, -EINVAL if cpu_mask is NULL
 */
int sched_getaffinity(tid_t tid, uint32_t *cpu_mask) {
    if (!cpu_mask) {
        return -22;  /* -EINVAL */
    }

    extern thread_t *thread_get(tid_t tid);
    thread_t *thread = thread_get(tid);
    if (!thread) {
        return -3;  /* -ESRCH */
    }

    *cpu_mask = thread->cpu_mask;
    return 0;
}
