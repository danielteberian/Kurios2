/* sem.c - POSIX Semaphores Implementation */

#include "sem.h"
#include "../mm/slab.h"
#include "../lib/string.h"
#include "../debug/debug.h"
#include "../sched/thread.h"
#include "../sched/sched.h"
#include "../fs/vfs.h"  /* For O_CREAT, O_EXCL */

/* Global semaphore table */
static sem_t *semaphores[SEM_MAX];
static spinlock_t sem_table_lock = SPINLOCK_INIT;

/* Per-process semaphore handle table (simplified) */
#define SEM_HANDLE_MAX 64
static sem_t *sem_handles[SEM_HANDLE_MAX];
static spinlock_t sem_handle_lock = SPINLOCK_INIT;

/*
 * Initialize semaphore subsystem
 */
void sem_init_subsystem(void) {
    memset(semaphores, 0, sizeof(semaphores));
    memset(sem_handles, 0, sizeof(sem_handles));
    spin_init(&sem_table_lock);
    spin_init(&sem_handle_lock);
    INFO("POSIX semaphores initialized");
}

/*
 * Find a semaphore by name
 */
static sem_t *sem_find_by_name(const char *name) {
    for (int i = 0; i < SEM_MAX; i++) {
        if (semaphores[i] && strcmp(semaphores[i]->name, name) == 0) {
            return semaphores[i];
        }
    }
    return NULL;
}

/*
 * Allocate a semaphore handle
 */
static int sem_alloc_handle(sem_t *sem) {
    uint64_t flags = spin_lock_irqsave(&sem_handle_lock);

    for (int i = 0; i < SEM_HANDLE_MAX; i++) {
        if (!sem_handles[i]) {
            sem_handles[i] = sem;
            spin_unlock_irqrestore(&sem_handle_lock, flags);
            return i;
        }
    }

    spin_unlock_irqrestore(&sem_handle_lock, flags);
    return -EMFILE;  /* Too many open semaphores */
}

/*
 * Get semaphore from handle
 */
static sem_t *sem_get_by_handle(int handle) {
    if (handle < 0 || handle >= SEM_HANDLE_MAX) {
        return NULL;
    }

    uint64_t flags = spin_lock_irqsave(&sem_handle_lock);
    sem_t *sem = sem_handles[handle];
    spin_unlock_irqrestore(&sem_handle_lock, flags);

    return sem;
}

/*
 * Free a semaphore handle
 */
static void sem_free_handle(int handle) {
    if (handle < 0 || handle >= SEM_HANDLE_MAX) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&sem_handle_lock);
    sem_handles[handle] = NULL;
    spin_unlock_irqrestore(&sem_handle_lock, flags);
}

/*
 * Add a thread to the wait queue
 */
static int sem_add_waiter(sem_t *sem, uint64_t thread_id) {
    if (sem->waiter_count >= sem->waiter_capacity) {
        /* Expand waiter array */
        int new_capacity = sem->waiter_capacity == 0 ? 4 : sem->waiter_capacity * 2;
        uint64_t *new_waiters = kmalloc(new_capacity * sizeof(uint64_t));
        if (!new_waiters) {
            return -ENOMEM;
        }

        if (sem->waiters) {
            memcpy(new_waiters, sem->waiters, sem->waiter_count * sizeof(uint64_t));
            kfree(sem->waiters);
        }

        sem->waiters = new_waiters;
        sem->waiter_capacity = new_capacity;
    }

    sem->waiters[sem->waiter_count++] = thread_id;
    return 0;
}

/*
 * Remove and return the first waiter
 */
static uint64_t sem_remove_waiter(sem_t *sem) {
    if (sem->waiter_count == 0) {
        return 0;
    }

    uint64_t thread_id = sem->waiters[0];

    /* Shift remaining waiters */
    for (int i = 1; i < sem->waiter_count; i++) {
        sem->waiters[i - 1] = sem->waiters[i];
    }

    sem->waiter_count--;
    return thread_id;
}

/*
 * Open a named semaphore
 */
int sem_open_syscall(const char *name, int oflag, mode_t mode, unsigned int value) {
    (void)mode;  /* Mode not used for now */

    if (!name || name[0] != '/') {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&sem_table_lock);

    /* Check if already exists */
    sem_t *sem = sem_find_by_name(name);

    if (sem) {
        /* Semaphore exists */
        if (oflag & O_EXCL) {
            spin_unlock_irqrestore(&sem_table_lock, flags);
            return -EEXIST;
        }

        if (sem->unlinked) {
            spin_unlock_irqrestore(&sem_table_lock, flags);
            return -ENOENT;
        }

        sem->refcount++;
        spin_unlock_irqrestore(&sem_table_lock, flags);

        /* Allocate handle */
        return sem_alloc_handle(sem);
    }

    /* Create new semaphore if O_CREAT specified */
    if (!(oflag & O_CREAT)) {
        spin_unlock_irqrestore(&sem_table_lock, flags);
        return -ENOENT;
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < SEM_MAX; i++) {
        if (!semaphores[i]) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        spin_unlock_irqrestore(&sem_table_lock, flags);
        return -ENOSPC;  /* No free slots */
    }

    /* Allocate semaphore */
    sem = kmalloc(sizeof(sem_t));
    if (!sem) {
        spin_unlock_irqrestore(&sem_table_lock, flags);
        return -ENOMEM;
    }

    memset(sem, 0, sizeof(sem_t));
    strncpy(sem->name, name, sizeof(sem->name) - 1);
    sem->name[sizeof(sem->name) - 1] = '\0';
    sem->value = (int)value;
    sem->refcount = 1;
    sem->unlinked = false;
    spin_init(&sem->lock);
    sem->waiters = NULL;
    sem->waiter_count = 0;
    sem->waiter_capacity = 0;

    semaphores[slot] = sem;
    spin_unlock_irqrestore(&sem_table_lock, flags);

    /* Allocate handle */
    return sem_alloc_handle(sem);
}

/*
 * Close a semaphore
 */
int sem_close_syscall(int sem_id) {
    sem_t *sem = sem_get_by_handle(sem_id);
    if (!sem) {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&sem->lock);
    sem->refcount--;

    bool should_free = (sem->refcount == 0 && sem->unlinked);
    spin_unlock_irqrestore(&sem->lock, flags);

    /* Free handle */
    sem_free_handle(sem_id);

    /* If unlinked and no references, free it */
    if (should_free) {
        uint64_t table_flags = spin_lock_irqsave(&sem_table_lock);
        for (int i = 0; i < SEM_MAX; i++) {
            if (semaphores[i] == sem) {
                semaphores[i] = NULL;
                break;
            }
        }
        spin_unlock_irqrestore(&sem_table_lock, table_flags);

        if (sem->waiters) {
            kfree(sem->waiters);
        }
        kfree(sem);
    }

    return 0;
}

/*
 * Remove a named semaphore
 */
int sem_unlink_syscall(const char *name) {
    if (!name || name[0] != '/') {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&sem_table_lock);

    sem_t *sem = sem_find_by_name(name);
    if (!sem) {
        spin_unlock_irqrestore(&sem_table_lock, flags);
        return -ENOENT;
    }

    spin_unlock_irqrestore(&sem_table_lock, flags);

    uint64_t sem_flags = spin_lock_irqsave(&sem->lock);
    sem->unlinked = true;

    /* If no references, free it immediately */
    if (sem->refcount == 0) {
        spin_unlock_irqrestore(&sem->lock, sem_flags);

        uint64_t table_flags = spin_lock_irqsave(&sem_table_lock);
        for (int i = 0; i < SEM_MAX; i++) {
            if (semaphores[i] == sem) {
                semaphores[i] = NULL;
                break;
            }
        }
        spin_unlock_irqrestore(&sem_table_lock, table_flags);

        if (sem->waiters) {
            kfree(sem->waiters);
        }
        kfree(sem);
    } else {
        spin_unlock_irqrestore(&sem->lock, sem_flags);
    }

    return 0;
}

/*
 * Wait on a semaphore (decrement, block if zero)
 */
int sem_wait_syscall(int sem_id) {
    sem_t *sem = sem_get_by_handle(sem_id);
    if (!sem) {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&sem->lock);

    while (sem->value == 0) {
        /* Add current thread to wait queue */
        thread_t *current = thread_current();
        if (!current) {
            spin_unlock_irqrestore(&sem->lock, flags);
            return -EINVAL;
        }

        int ret = sem_add_waiter(sem, (uint64_t)current);
        if (ret < 0) {
            spin_unlock_irqrestore(&sem->lock, flags);
            return ret;
        }

        /* Block the thread (thread_block blocks the current thread) */
        spin_unlock_irqrestore(&sem->lock, flags);
        thread_block();

        /* Yield to scheduler */
        sched_reschedule();

        /* When we wake up, reacquire lock and check value */
        flags = spin_lock_irqsave(&sem->lock);
    }

    /* Decrement semaphore value */
    sem->value--;
    spin_unlock_irqrestore(&sem->lock, flags);

    return 0;
}

/*
 * Post to a semaphore (increment, wake one waiter)
 */
int sem_post_syscall(int sem_id) {
    sem_t *sem = sem_get_by_handle(sem_id);
    if (!sem) {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&sem->lock);

    /* Increment semaphore value */
    sem->value++;

    /* Wake one waiting thread if any */
    if (sem->waiter_count > 0) {
        uint64_t thread_id = sem_remove_waiter(sem);
        if (thread_id != 0) {
            thread_t *waiter = (thread_t *)thread_id;
            thread_unblock(waiter);
        }
    }

    spin_unlock_irqrestore(&sem->lock, flags);

    return 0;
}

/*
 * Try to wait on a semaphore (non-blocking)
 */
int sem_trywait_syscall(int sem_id) {
    sem_t *sem = sem_get_by_handle(sem_id);
    if (!sem) {
        return -EINVAL;
    }

    uint64_t flags = spin_lock_irqsave(&sem->lock);

    if (sem->value == 0) {
        spin_unlock_irqrestore(&sem->lock, flags);
        return -EAGAIN;  /* Would block */
    }

    /* Decrement semaphore value */
    sem->value--;
    spin_unlock_irqrestore(&sem->lock, flags);

    return 0;
}

/*
 * Get semaphore value
 */
int sem_getvalue_syscall(int sem_id, int *sval) {
    sem_t *sem = sem_get_by_handle(sem_id);
    if (!sem) {
        return -EINVAL;
    }

    if (!sval) {
        return -EFAULT;
    }

    uint64_t flags = spin_lock_irqsave(&sem->lock);
    *sval = sem->value;
    spin_unlock_irqrestore(&sem->lock, flags);

    return 0;
}
