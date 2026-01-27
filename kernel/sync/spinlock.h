/* spinlock.h - Basic spinlock implementation */
#ifndef _SYNC_SPINLOCK_H
#define _SYNC_SPINLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "../arch/x86_64/cpu.h"

/* Spinlock type - just an atomic flag */
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

/* Static initializer */
#define SPINLOCK_INIT { .lock = 0 }

/* Initialize a spinlock */
static inline void spin_init(spinlock_t *lock) {
    lock->lock = 0;
}

/* Try to acquire lock, return true if successful */
static inline bool spin_trylock(spinlock_t *lock) {
    uint32_t expected = 0;
    return __atomic_compare_exchange_n(&lock->lock, &expected, 1,
                                       false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

/* Acquire spinlock (busy-wait) */
static inline void spin_lock(spinlock_t *lock) {
    while (!spin_trylock(lock)) {
        /* Spin with pause to reduce bus contention */
        while (__atomic_load_n(&lock->lock, __ATOMIC_RELAXED)) {
            cpu_pause();
        }
    }
}

/* Release spinlock */
static inline void spin_unlock(spinlock_t *lock) {
    __atomic_store_n(&lock->lock, 0, __ATOMIC_RELEASE);
}

/* Check if lock is held (for debugging) */
static inline bool spin_is_locked(spinlock_t *lock) {
    return __atomic_load_n(&lock->lock, __ATOMIC_RELAXED) != 0;
}

/*
 * IRQ-safe spinlock operations
 * These disable interrupts while holding the lock
 */

/* Acquire lock and disable interrupts, returns previous interrupt state */
static inline uint64_t spin_lock_irqsave(spinlock_t *lock) {
    uint64_t flags = read_rflags();
    cli();
    spin_lock(lock);
    return flags;
}

/* Release lock and restore interrupt state */
static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    spin_unlock(lock);
    if (flags & 0x200) {  /* IF flag was set */
        sti();
    }
}

/* Simple IRQ disable/enable versions (don't save state) */
static inline void spin_lock_irq(spinlock_t *lock) {
    cli();
    spin_lock(lock);
}

static inline void spin_unlock_irq(spinlock_t *lock) {
    spin_unlock(lock);
    sti();
}

#endif /* _SYNC_SPINLOCK_H */
