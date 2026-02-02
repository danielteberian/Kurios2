/* io_sched.h - I/O Scheduler Interface */
#ifndef _KERNEL_IO_SCHED_H
#define _KERNEL_IO_SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include "block.h"

/* Forward declarations */
struct io_scheduler;
struct block_device;

/*
 * I/O Scheduler operations
 */
typedef struct io_scheduler_ops {
    const char *name;

    /* Initialize scheduler for a device */
    int (*init)(struct io_scheduler *sched, struct block_device *dev);

    /* Cleanup scheduler */
    void (*exit)(struct io_scheduler *sched);

    /* Add a request to the scheduler queue */
    void (*add_request)(struct io_scheduler *sched, block_request_t *req);

    /* Get next request to dispatch (returns NULL if none) */
    block_request_t *(*dispatch)(struct io_scheduler *sched);

    /* Try to merge request with existing ones (returns true if merged) */
    bool (*merge)(struct io_scheduler *sched, block_request_t *req);

    /* Called when a request completes */
    void (*completed)(struct io_scheduler *sched, block_request_t *req);
} io_scheduler_ops_t;

/*
 * I/O Scheduler instance
 */
typedef struct io_scheduler {
    io_scheduler_ops_t *ops;        /* Scheduler operations */
    struct block_device *dev;       /* Associated block device */
    void *private;                  /* Scheduler-specific data */

    /* Queue */
    block_request_t *queue_head;
    block_request_t *queue_tail;
    uint32_t queue_len;

    /* Statistics */
    uint64_t requests_added;
    uint64_t requests_dispatched;
    uint64_t requests_merged;
} io_scheduler_t;

/*
 * Initialize I/O scheduler subsystem
 */
void io_sched_init(void);

/*
 * Create a scheduler instance for a device
 *
 * @param dev   Block device
 * @param name  Scheduler name ("noop", etc.) or NULL for default
 * @return Scheduler instance or NULL on failure
 */
io_scheduler_t *io_sched_create(struct block_device *dev, const char *name);

/*
 * Destroy a scheduler instance
 */
void io_sched_destroy(io_scheduler_t *sched);

/*
 * Add a request to the scheduler
 */
void io_sched_add_request(io_scheduler_t *sched, block_request_t *req);

/*
 * Dispatch the next request
 *
 * @return Request to dispatch, or NULL if queue is empty
 */
block_request_t *io_sched_dispatch(io_scheduler_t *sched);

/*
 * Notify scheduler that a request completed
 */
void io_sched_completed(io_scheduler_t *sched, block_request_t *req);

/*
 * Get default scheduler operations (NOOP)
 */
io_scheduler_ops_t *io_sched_get_noop(void);

#endif /* _KERNEL_IO_SCHED_H */
