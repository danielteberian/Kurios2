/* block.c - Block Device Layer Implementation */

#include "block.h"
#include "partition.h"
#include "../debug/debug.h"
#include "../mm/slab.h"
#include "../lib/string.h"
#include "../sync/spinlock.h"

/* List of registered block devices */
static block_device_t *block_devices = NULL;
static uint32_t block_device_count = 0;

/* Slab cache for block requests */
static kmem_cache_t *block_request_cache = NULL;

/*
 * Initialize block device subsystem
 */
void block_init(void) {
    INFO("Initializing block device subsystem");

    /* Create request cache */
    block_request_cache = kmem_cache_create("block_request",
                                             sizeof(block_request_t), 0, 0);
    if (!block_request_cache) {
        ERROR("Failed to create block request cache");
        return;
    }

    INFO("Block device subsystem initialized");
}

/*
 * Register a new block device
 */
int block_register(block_device_t *dev) {
    if (!dev || !dev->name[0]) {
        return -22;  /* EINVAL */
    }

    /* Check for duplicate name */
    if (block_find(dev->name)) {
        ERROR("Block device '%s' already registered", dev->name);
        return -17;  /* EEXIST */
    }

    /* Check limit */
    if (block_device_count >= BLOCK_MAX_DEVICES) {
        ERROR("Too many block devices");
        return -28;  /* ENOSPC */
    }

    /* Calculate capacity if not set */
    if (dev->capacity == 0 && dev->sector_count > 0) {
        dev->capacity = dev->sector_count * dev->sector_size;
    }

    /* Add to list */
    dev->index = block_device_count++;
    dev->next = block_devices;
    block_devices = dev;

    INFO("Registered block device: %s (%llu sectors, %llu MB)",
         dev->name, dev->sector_count,
         dev->capacity / (1024 * 1024));

    /* Scan for partitions (only for whole-disk devices, not partition devices) */
    /* Partition devices have names like "vda1", whole disks have names like "vda" */
    int namelen = strlen(dev->name);
    char last = dev->name[namelen - 1];
    if (last < '0' || last > '9') {
        /* Not a partition device - scan for partitions */
        partition_scan(dev);
    }

    return 0;
}

/*
 * Unregister a block device
 */
void block_unregister(block_device_t *dev) {
    if (!dev) return;

    /* Remove from list */
    block_device_t **pp = &block_devices;
    while (*pp) {
        if (*pp == dev) {
            *pp = dev->next;
            block_device_count--;
            INFO("Unregistered block device: %s", dev->name);
            return;
        }
        pp = &(*pp)->next;
    }

    WARN("Block device '%s' not found in list", dev->name);
}

/*
 * Find block device by name
 */
block_device_t *block_find(const char *name) {
    if (!name) return NULL;

    for (block_device_t *dev = block_devices; dev; dev = dev->next) {
        if (strcmp(dev->name, name) == 0) {
            return dev;
        }
    }
    return NULL;
}

/*
 * Read sectors from a block device
 */
int block_read(block_device_t *dev, uint64_t sector, uint32_t count, void *buf) {
    if (!dev || !buf || count == 0) {
        return -22;  /* EINVAL */
    }

    /* Check bounds */
    if (sector + count > dev->sector_count) {
        ERROR("block_read: out of bounds (sector=%llu, count=%u, max=%llu)",
              sector, count, dev->sector_count);
        return -22;  /* EINVAL */
    }

    /* Check ops */
    if (!dev->ops || !dev->ops->read) {
        ERROR("block_read: no read operation for %s", dev->name);
        return -5;  /* EIO */
    }

    int result = dev->ops->read(dev, sector, count, buf);
    if (result == 0) {
        dev->read_sectors += count;
        dev->read_ops++;
    }
    return result;
}

/*
 * Write sectors to a block device
 */
int block_write(block_device_t *dev, uint64_t sector, uint32_t count, const void *buf) {
    if (!dev || !buf || count == 0) {
        return -22;  /* EINVAL */
    }

    /* Check read-only */
    if (dev->read_only) {
        ERROR("block_write: device %s is read-only", dev->name);
        return -30;  /* EROFS */
    }

    /* Check bounds */
    if (sector + count > dev->sector_count) {
        ERROR("block_write: out of bounds (sector=%llu, count=%u, max=%llu)",
              sector, count, dev->sector_count);
        return -22;  /* EINVAL */
    }

    /* Check ops */
    if (!dev->ops || !dev->ops->write) {
        ERROR("block_write: no write operation for %s", dev->name);
        return -5;  /* EIO */
    }

    int result = dev->ops->write(dev, sector, count, buf);
    if (result == 0) {
        dev->write_sectors += count;
        dev->write_ops++;
    }
    return result;
}

/*
 * Flush pending writes
 */
int block_flush(block_device_t *dev) {
    if (!dev) {
        return -22;  /* EINVAL */
    }

    if (dev->ops && dev->ops->flush) {
        return dev->ops->flush(dev);
    }

    return 0;  /* No flush operation = success */
}

/*
 * Allocate a block request
 */
block_request_t *block_request_alloc(void) {
    if (!block_request_cache) {
        return NULL;
    }

    block_request_t *req = kmem_cache_alloc(block_request_cache);
    if (req) {
        memset(req, 0, sizeof(*req));
    }
    return req;
}

/*
 * Free a block request
 */
void block_request_free(block_request_t *req) {
    if (req && block_request_cache) {
        kmem_cache_free(block_request_cache, req);
    }
}

/*
 * Submit an asynchronous block request
 */
int block_submit(block_device_t *dev, block_request_t *req) {
    if (!dev || !req) {
        return -22;  /* EINVAL */
    }

    /* Use driver's submit if available */
    if (dev->ops && dev->ops->submit) {
        return dev->ops->submit(dev, req);
    }

    /* Otherwise, do synchronous I/O */
    int result;
    switch (req->type) {
        case BLOCK_READ:
            result = block_read(dev, req->sector, req->count, req->buffer);
            break;
        case BLOCK_WRITE:
            result = block_write(dev, req->sector, req->count, req->buffer);
            break;
        case BLOCK_FLUSH:
            result = block_flush(dev);
            break;
        default:
            result = -22;
    }

    req->status = result;
    req->complete = true;
    return result;
}

/*
 * Wait for a block request to complete
 */
int block_wait(block_request_t *req) {
    if (!req) {
        return -22;  /* EINVAL */
    }

    /* Busy wait for completion */
    while (!req->complete) {
        __asm__ volatile("pause");
    }

    return req->status;
}

/*
 * List all block devices
 */
void block_list_devices(void) {
    kprintf("=== Block Devices ===\n");
    kprintf("%-8s %-12s %-10s %-10s %s\n",
            "Name", "Sectors", "Size", "R/W Ops", "Flags");

    for (block_device_t *dev = block_devices; dev; dev = dev->next) {
        uint64_t size_mb = dev->capacity / (1024 * 1024);
        kprintf("%-8s %-12llu %-7lluMB %-5llu/%-5llu %s%s\n",
                dev->name,
                dev->sector_count,
                size_mb,
                dev->read_ops,
                dev->write_ops,
                dev->read_only ? "RO " : "RW ",
                dev->removable ? "RM" : "");
    }

    if (!block_devices) {
        kprintf("  (no devices)\n");
    }
    kprintf("\n");
}

/*
 * Lock a block device's queue
 */
static inline uint64_t block_queue_lock(block_device_t *dev) {
    spinlock_t *lock = (spinlock_t *)&dev->queue_lock;
    return spin_lock_irqsave(lock);
}

/*
 * Unlock a block device's queue
 */
static inline void block_queue_unlock(block_device_t *dev, uint64_t flags) {
    spinlock_t *lock = (spinlock_t *)&dev->queue_lock;
    spin_unlock_irqrestore(lock, flags);
}

/*
 * Add request to queue (must hold lock)
 */
static void block_queue_add_locked(block_device_t *dev, block_request_t *req) {
    req->next = NULL;

    if (dev->queue_tail) {
        dev->queue_tail->next = req;
        dev->queue_tail = req;
    } else {
        dev->queue_head = req;
        dev->queue_tail = req;
    }
}

/*
 * Remove request from queue head (must hold lock)
 * Note: Currently unused but will be used by I/O scheduler
 */
__attribute__((unused))
static block_request_t *block_queue_pop_locked(block_device_t *dev) {
    block_request_t *req = dev->queue_head;
    if (req) {
        dev->queue_head = req->next;
        if (!dev->queue_head) {
            dev->queue_tail = NULL;
        }
        req->next = NULL;
    }
    return req;
}

/*
 * Submit an async block request with callback
 */
int block_submit_async(block_device_t *dev, block_request_t *req,
                       block_callback_t callback, void *ctx) {
    if (!dev || !req) {
        return -22;  /* EINVAL */
    }

    req->callback = callback;
    req->callback_ctx = ctx;
    req->complete = false;
    req->status = 0;

    /* Add to queue */
    uint64_t flags = block_queue_lock(dev);
    block_queue_add_locked(dev, req);
    dev->in_flight++;
    block_queue_unlock(dev, flags);

    /* Use driver's submit if available */
    if (dev->ops && dev->ops->submit) {
        return dev->ops->submit(dev, req);
    }

    /* Otherwise, do synchronous I/O (fallback) */
    int result;
    switch (req->type) {
        case BLOCK_READ:
            result = block_read(dev, req->sector, req->count, req->buffer);
            break;
        case BLOCK_WRITE:
            result = block_write(dev, req->sector, req->count, req->buffer);
            break;
        case BLOCK_FLUSH:
            result = block_flush(dev);
            break;
        default:
            result = -22;
    }

    /* Complete the request */
    req->status = result;
    req->complete = true;

    /* Call callback if provided */
    if (req->callback) {
        req->callback(req, req->callback_ctx);
    }

    flags = block_queue_lock(dev);
    dev->in_flight--;
    block_queue_unlock(dev, flags);

    return result;
}

/*
 * Complete a block request (called by driver from IRQ)
 */
void block_complete(block_device_t *dev, block_request_t *req) {
    if (!dev || !req) return;

    /* Mark as complete */
    req->complete = true;

    /* Call callback if provided */
    if (req->callback) {
        req->callback(req, req->callback_ctx);
    }

    /* Decrement in-flight counter */
    uint64_t flags = block_queue_lock(dev);
    if (dev->in_flight > 0) {
        dev->in_flight--;
    }
    block_queue_unlock(dev, flags);
}
