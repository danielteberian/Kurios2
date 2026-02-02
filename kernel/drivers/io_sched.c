/* io_sched.c - I/O Scheduler Implementation */

#include "io_sched.h"
#include "block.h"
#include "../debug/debug.h"
#include "../mm/slab.h"
#include "../lib/string.h"
#include "../sync/spinlock.h"

/* Slab cache for scheduler instances */
static kmem_cache_t *io_sched_cache = NULL;

/* Spinlock for scheduler operations */
static spinlock_t io_sched_lock = SPINLOCK_INIT;

/* ============================================================================
 * NOOP Scheduler Implementation
 * Simple FIFO scheduler with optional request merging for adjacent sectors
 * ============================================================================ */

/*
 * NOOP: Initialize
 */
static int noop_init(io_scheduler_t *sched, block_device_t *dev) {
    sched->queue_head = NULL;
    sched->queue_tail = NULL;
    sched->queue_len = 0;
    DEBUG("NOOP scheduler initialized for %s", dev->name);
    return 0;
}

/*
 * NOOP: Cleanup
 */
static void noop_exit(io_scheduler_t *sched) {
    /* Free any remaining requests in queue */
    block_request_t *req = sched->queue_head;
    while (req) {
        block_request_t *next = req->next;
        block_request_free(req);
        req = next;
    }
    sched->queue_head = NULL;
    sched->queue_tail = NULL;
    sched->queue_len = 0;
}

/*
 * NOOP: Add request to queue (FIFO)
 */
static void noop_add_request(io_scheduler_t *sched, block_request_t *req) {
    req->next = NULL;

    if (sched->queue_tail) {
        sched->queue_tail->next = req;
        sched->queue_tail = req;
    } else {
        sched->queue_head = req;
        sched->queue_tail = req;
    }

    sched->queue_len++;
    sched->requests_added++;
}

/*
 * NOOP: Dispatch next request
 */
static block_request_t *noop_dispatch(io_scheduler_t *sched) {
    block_request_t *req = sched->queue_head;

    if (req) {
        sched->queue_head = req->next;
        if (!sched->queue_head) {
            sched->queue_tail = NULL;
        }
        req->next = NULL;
        sched->queue_len--;
        sched->requests_dispatched++;
    }

    return req;
}

/*
 * NOOP: Try to merge request with existing
 *
 * Merges if:
 * - Same request type (both read or both write)
 * - Adjacent sectors
 */
static bool noop_merge(io_scheduler_t *sched, block_request_t *new_req) {
    if (!sched->queue_head) {
        return false;
    }

    /* Search queue for mergeable request */
    for (block_request_t *req = sched->queue_head; req; req = req->next) {
        /* Must be same type */
        if (req->type != new_req->type) {
            continue;
        }

        /* Check if new request follows this one */
        if (req->sector + req->count == new_req->sector) {
            /* Extend this request */
            req->count += new_req->count;
            sched->requests_merged++;
            return true;
        }

        /* Check if new request precedes this one */
        if (new_req->sector + new_req->count == req->sector) {
            /* Prepend new sectors to this request */
            req->sector = new_req->sector;
            req->count += new_req->count;
            sched->requests_merged++;
            return true;
        }
    }

    return false;
}

/*
 * NOOP: Request completed (nothing special to do)
 */
static void noop_completed(io_scheduler_t *sched, block_request_t *req) {
    (void)sched;
    (void)req;
}

/* NOOP scheduler operations */
static io_scheduler_ops_t noop_ops = {
    .name = "noop",
    .init = noop_init,
    .exit = noop_exit,
    .add_request = noop_add_request,
    .dispatch = noop_dispatch,
    .merge = noop_merge,
    .completed = noop_completed,
};

/*
 * Get NOOP scheduler operations
 */
io_scheduler_ops_t *io_sched_get_noop(void) {
    return &noop_ops;
}

/* ============================================================================
 * Scheduler Subsystem API
 * ============================================================================ */

/*
 * Initialize I/O scheduler subsystem
 */
void io_sched_init(void) {
    INFO("Initializing I/O scheduler subsystem");

    io_sched_cache = kmem_cache_create("io_sched",
                                        sizeof(io_scheduler_t), 0, 0);
    if (!io_sched_cache) {
        ERROR("Failed to create I/O scheduler cache");
        return;
    }

    INFO("I/O scheduler subsystem initialized (NOOP default)");
}

/*
 * Create a scheduler instance for a device
 */
io_scheduler_t *io_sched_create(block_device_t *dev, const char *name) {
    if (!dev) return NULL;

    /* Default to NOOP scheduler */
    io_scheduler_ops_t *ops = &noop_ops;

    /* Could add other schedulers here based on name */
    if (name && strcmp(name, "noop") != 0) {
        WARN("Unknown scheduler '%s', using NOOP", name);
    }

    /* Allocate scheduler instance */
    io_scheduler_t *sched = kmem_cache_alloc(io_sched_cache);
    if (!sched) {
        ERROR("Failed to allocate I/O scheduler for %s", dev->name);
        return NULL;
    }

    memset(sched, 0, sizeof(*sched));
    sched->ops = ops;
    sched->dev = dev;

    /* Initialize */
    if (ops->init && ops->init(sched, dev) != 0) {
        kmem_cache_free(io_sched_cache, sched);
        return NULL;
    }

    DEBUG("Created %s scheduler for %s", ops->name, dev->name);
    return sched;
}

/*
 * Destroy a scheduler instance
 */
void io_sched_destroy(io_scheduler_t *sched) {
    if (!sched) return;

    if (sched->ops && sched->ops->exit) {
        sched->ops->exit(sched);
    }

    kmem_cache_free(io_sched_cache, sched);
}

/*
 * Add a request to the scheduler
 */
void io_sched_add_request(io_scheduler_t *sched, block_request_t *req) {
    if (!sched || !req) return;

    uint64_t flags = spin_lock_irqsave(&io_sched_lock);

    /* Try to merge first */
    if (sched->ops->merge && sched->ops->merge(sched, req)) {
        /* Request was merged - free the new one */
        spin_unlock_irqrestore(&io_sched_lock, flags);
        block_request_free(req);
        return;
    }

    /* Add to queue */
    if (sched->ops->add_request) {
        sched->ops->add_request(sched, req);
    }

    spin_unlock_irqrestore(&io_sched_lock, flags);
}

/*
 * Dispatch the next request
 */
block_request_t *io_sched_dispatch(io_scheduler_t *sched) {
    if (!sched || !sched->ops->dispatch) return NULL;

    uint64_t flags = spin_lock_irqsave(&io_sched_lock);
    block_request_t *req = sched->ops->dispatch(sched);
    spin_unlock_irqrestore(&io_sched_lock, flags);

    return req;
}

/*
 * Notify scheduler that a request completed
 */
void io_sched_completed(io_scheduler_t *sched, block_request_t *req) {
    if (!sched || !req) return;

    if (sched->ops->completed) {
        uint64_t flags = spin_lock_irqsave(&io_sched_lock);
        sched->ops->completed(sched, req);
        spin_unlock_irqrestore(&io_sched_lock, flags);
    }
}
