/* rwlock.c - Read-Write Lock Implementation */

#include "rwlock.h"
#include "../arch/x86_64/cpu.h"

/*
 * Initialize a read-write lock
 */
void rwlock_init(rwlock_t *rwlock) {
    if (!rwlock) return;

    spin_init(&rwlock->lock);
    rwlock->readers = 0;
    rwlock->writer = false;
}

/*
 * Acquire read lock (multiple readers allowed)
 * Blocks until no writer is active
 */
void rwlock_read_lock(rwlock_t *rwlock) {
    if (!rwlock) return;

    uint64_t flags = spin_lock_irqsave(&rwlock->lock);

    /* Wait until no writer is active */
    while (rwlock->writer) {
        spin_unlock_irqrestore(&rwlock->lock, flags);
        cpu_pause();  /* CPU hint: we're spinning */
        flags = spin_lock_irqsave(&rwlock->lock);
    }

    /* Increment reader count */
    rwlock->readers++;

    spin_unlock_irqrestore(&rwlock->lock, flags);
}

/*
 * Release read lock
 */
void rwlock_read_unlock(rwlock_t *rwlock) {
    if (!rwlock) return;

    uint64_t flags = spin_lock_irqsave(&rwlock->lock);

    /* Decrement reader count */
    if (rwlock->readers > 0) {
        rwlock->readers--;
    }

    spin_unlock_irqrestore(&rwlock->lock, flags);
}

/*
 * Acquire write lock (exclusive access)
 * Blocks until no readers or writers are active
 */
void rwlock_write_lock(rwlock_t *rwlock) {
    if (!rwlock) return;

    uint64_t flags = spin_lock_irqsave(&rwlock->lock);

    /* Wait until no writer and no readers */
    while (rwlock->writer || rwlock->readers > 0) {
        spin_unlock_irqrestore(&rwlock->lock, flags);
        cpu_pause();  /* CPU hint: we're spinning */
        flags = spin_lock_irqsave(&rwlock->lock);
    }

    /* Acquire exclusive write access */
    rwlock->writer = true;

    spin_unlock_irqrestore(&rwlock->lock, flags);
}

/*
 * Release write lock
 */
void rwlock_write_unlock(rwlock_t *rwlock) {
    if (!rwlock) return;

    uint64_t flags = spin_lock_irqsave(&rwlock->lock);

    /* Release write access */
    rwlock->writer = false;

    spin_unlock_irqrestore(&rwlock->lock, flags);
}

/*
 * Try to acquire read lock without blocking
 * Returns true if lock was acquired, false otherwise
 */
bool rwlock_try_read_lock(rwlock_t *rwlock) {
    if (!rwlock) return false;

    uint64_t flags = spin_lock_irqsave(&rwlock->lock);

    /* Can acquire if no writer is active */
    if (!rwlock->writer) {
        rwlock->readers++;
        spin_unlock_irqrestore(&rwlock->lock, flags);
        return true;
    }

    spin_unlock_irqrestore(&rwlock->lock, flags);
    return false;
}

/*
 * Try to acquire write lock without blocking
 * Returns true if lock was acquired, false otherwise
 */
bool rwlock_try_write_lock(rwlock_t *rwlock) {
    if (!rwlock) return false;

    uint64_t flags = spin_lock_irqsave(&rwlock->lock);

    /* Can acquire if no writer and no readers */
    if (!rwlock->writer && rwlock->readers == 0) {
        rwlock->writer = true;
        spin_unlock_irqrestore(&rwlock->lock, flags);
        return true;
    }

    spin_unlock_irqrestore(&rwlock->lock, flags);
    return false;
}
