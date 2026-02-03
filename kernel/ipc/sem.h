/* sem.h - POSIX Semaphores */

#ifndef SEM_H
#define SEM_H

#include "../include/types.h"
#include "../sync/spinlock.h"

/* Maximum number of semaphores */
#define SEM_MAX 256

/* Semaphore structure */
typedef struct semaphore {
    char name[256];             /* Name (for named semaphores) */
    int value;                  /* Current semaphore value */
    int refcount;               /* Reference count */
    bool unlinked;              /* Marked for deletion */
    spinlock_t lock;
    /* Wait queue - simple array of waiting threads */
    uint64_t *waiters;          /* Array of thread IDs */
    int waiter_count;           /* Number of waiting threads */
    int waiter_capacity;        /* Capacity of waiters array */
} sem_t;

/*
 * Initialize semaphore subsystem
 */
void sem_init_subsystem(void);

/*
 * Open a named semaphore
 *
 * name: Semaphore name (must start with '/')
 * oflag: Open flags (O_CREAT, O_EXCL)
 * mode: Permissions (when creating)
 * value: Initial value (when creating)
 *
 * Returns: Semaphore ID on success, negative errno on failure
 */
int sem_open_syscall(const char *name, int oflag, mode_t mode, unsigned int value);

/*
 * Close a semaphore
 *
 * sem_id: Semaphore ID from sem_open
 *
 * Returns: 0 on success, negative errno on failure
 */
int sem_close_syscall(int sem_id);

/*
 * Remove a named semaphore
 *
 * name: Semaphore name
 *
 * Returns: 0 on success, negative errno on failure
 */
int sem_unlink_syscall(const char *name);

/*
 * Wait on a semaphore (decrement, block if zero)
 *
 * sem_id: Semaphore ID
 *
 * Returns: 0 on success, negative errno on failure
 */
int sem_wait_syscall(int sem_id);

/*
 * Post to a semaphore (increment, wake one waiter)
 *
 * sem_id: Semaphore ID
 *
 * Returns: 0 on success, negative errno on failure
 */
int sem_post_syscall(int sem_id);

/*
 * Try to wait on a semaphore (non-blocking)
 *
 * sem_id: Semaphore ID
 *
 * Returns: 0 on success, -EAGAIN if would block, negative errno on other failure
 */
int sem_trywait_syscall(int sem_id);

/*
 * Get semaphore value
 *
 * sem_id: Semaphore ID
 * sval: Pointer to store value
 *
 * Returns: 0 on success, negative errno on failure
 */
int sem_getvalue_syscall(int sem_id, int *sval);

#endif /* SEM_H */
