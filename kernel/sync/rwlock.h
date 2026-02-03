/* rwlock.h - Read-Write Lock */
#ifndef _SYNC_RWLOCK_H
#define _SYNC_RWLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "spinlock.h"

/*
 * Read-Write Lock
 *
 * Allows multiple readers OR one writer (mutually exclusive).
 * Readers can acquire the lock concurrently, but writers get exclusive access.
 *
 * Usage:
 *   rwlock_t lock = RWLOCK_INIT;
 *   rwlock_read_lock(&lock);      // Multiple readers allowed
 *   ... read data ...
 *   rwlock_read_unlock(&lock);
 *
 *   rwlock_write_lock(&lock);     // Exclusive writer
 *   ... write data ...
 *   rwlock_write_unlock(&lock);
 */
typedef struct {
    spinlock_t lock;        /* Protects the rwlock state */
    int32_t readers;        /* Number of active readers (0 = none) */
    bool writer;            /* True if a writer holds the lock */
} rwlock_t;

/* Static initializer for read-write locks */
#define RWLOCK_INIT { .lock = SPINLOCK_INIT, .readers = 0, .writer = false }

/* Initialize a read-write lock */
void rwlock_init(rwlock_t *rwlock);

/* Acquire read lock (multiple readers allowed) */
void rwlock_read_lock(rwlock_t *rwlock);

/* Release read lock */
void rwlock_read_unlock(rwlock_t *rwlock);

/* Acquire write lock (exclusive access) */
void rwlock_write_lock(rwlock_t *rwlock);

/* Release write lock */
void rwlock_write_unlock(rwlock_t *rwlock);

/* Try to acquire read lock without blocking (returns true if acquired) */
bool rwlock_try_read_lock(rwlock_t *rwlock);

/* Try to acquire write lock without blocking (returns true if acquired) */
bool rwlock_try_write_lock(rwlock_t *rwlock);

#endif /* _SYNC_RWLOCK_H */
