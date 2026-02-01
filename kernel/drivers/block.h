/* block.h - Block Device Layer */
#ifndef _KERNEL_BLOCK_H
#define _KERNEL_BLOCK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Block device constants */
#define BLOCK_SECTOR_SIZE       512     /* Standard sector size */
#define BLOCK_MAX_DEVICES       16      /* Maximum block devices */
#define BLOCK_NAME_MAX          32      /* Maximum device name length */

/* Block request types */
typedef enum {
    BLOCK_READ,
    BLOCK_WRITE,
    BLOCK_FLUSH
} block_req_type_t;

/* Block request structure */
typedef struct block_request {
    block_req_type_t type;      /* Request type */
    uint64_t sector;            /* Starting sector */
    uint32_t count;             /* Number of sectors */
    void *buffer;               /* Data buffer */
    volatile bool complete;     /* Completion flag */
    volatile int status;        /* Result: 0 = success, <0 = error */
    struct block_request *next; /* Next request in queue */
} block_request_t;

/* Forward declaration */
struct block_device;

/* Block device operations */
typedef struct block_ops {
    int (*read)(struct block_device *dev, uint64_t sector, uint32_t count, void *buf);
    int (*write)(struct block_device *dev, uint64_t sector, uint32_t count, const void *buf);
    int (*flush)(struct block_device *dev);
    int (*submit)(struct block_device *dev, block_request_t *req);
    int (*poll)(struct block_device *dev);
} block_ops_t;

/* Block device structure */
typedef struct block_device {
    char name[BLOCK_NAME_MAX];          /* Device name (e.g., "vda", "sda") */
    uint32_t index;                     /* Device index */

    /* Geometry */
    uint64_t sector_count;              /* Total sectors */
    uint32_t sector_size;               /* Bytes per sector */
    uint64_t capacity;                  /* Total capacity in bytes */

    /* Capabilities */
    bool read_only;                     /* Device is read-only */
    bool removable;                     /* Device is removable */

    /* Operations */
    block_ops_t *ops;

    /* Request queue (simple FIFO) */
    block_request_t *queue_head;
    block_request_t *queue_tail;

    /* Statistics */
    uint64_t read_sectors;
    uint64_t write_sectors;
    uint64_t read_ops;
    uint64_t write_ops;

    /* Driver private data */
    void *private;

    /* List linkage */
    struct block_device *next;
} block_device_t;

/*
 * Initialize block device subsystem
 */
void block_init(void);

/*
 * Register a new block device
 *
 * @param dev  Device to register
 * @return 0 on success, negative error on failure
 */
int block_register(block_device_t *dev);

/*
 * Unregister a block device
 *
 * @param dev  Device to unregister
 */
void block_unregister(block_device_t *dev);

/*
 * Find block device by name
 *
 * @param name  Device name to find
 * @return Device pointer or NULL if not found
 */
block_device_t *block_find(const char *name);

/*
 * Read sectors from a block device
 *
 * @param dev     Block device
 * @param sector  Starting sector number
 * @param count   Number of sectors to read
 * @param buf     Buffer to read into (must be count * sector_size bytes)
 * @return 0 on success, negative error on failure
 */
int block_read(block_device_t *dev, uint64_t sector, uint32_t count, void *buf);

/*
 * Write sectors to a block device
 *
 * @param dev     Block device
 * @param sector  Starting sector number
 * @param count   Number of sectors to write
 * @param buf     Buffer to write from (must be count * sector_size bytes)
 * @return 0 on success, negative error on failure
 */
int block_write(block_device_t *dev, uint64_t sector, uint32_t count, const void *buf);

/*
 * Flush pending writes to a block device
 *
 * @param dev  Block device
 * @return 0 on success, negative error on failure
 */
int block_flush(block_device_t *dev);

/*
 * Allocate a block request structure
 */
block_request_t *block_request_alloc(void);

/*
 * Free a block request structure
 */
void block_request_free(block_request_t *req);

/*
 * Submit an asynchronous block request
 *
 * @param dev  Block device
 * @param req  Request to submit
 * @return 0 on success, negative error on failure
 */
int block_submit(block_device_t *dev, block_request_t *req);

/*
 * Wait for a block request to complete
 *
 * @param req  Request to wait for
 * @return 0 on success, negative error on failure
 */
int block_wait(block_request_t *req);

/*
 * List all block devices (for debugging)
 */
void block_list_devices(void);

#endif /* _KERNEL_BLOCK_H */
