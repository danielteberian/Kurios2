/* virtio_blk.c - Virtio Block Device Driver */

#include "virtio.h"
#include "../block.h"
#include "../../debug/debug.h"
#include "../../mm/slab.h"
#include "../../lib/string.h"
#include "../../arch/x86_64/idt.h"

/*
 * Virtio block device config (at VIRTIO_PCI_CONFIG)
 */
#define VIRTIO_BLK_CFG_CAPACITY     0   /* 64-bit: capacity in 512-byte sectors */
#define VIRTIO_BLK_CFG_SIZE_MAX     8   /* 32-bit: max segment size */
#define VIRTIO_BLK_CFG_SEG_MAX      12  /* 32-bit: max segments per request */
#define VIRTIO_BLK_CFG_GEOMETRY     16  /* 16+8+8: cylinders, heads, sectors */
#define VIRTIO_BLK_CFG_BLK_SIZE     20  /* 32-bit: block size (usually 512) */

/*
 * Virtio block feature bits
 */
#define VIRTIO_BLK_F_BARRIER        (1 << 0)   /* Barrier supported */
#define VIRTIO_BLK_F_SIZE_MAX       (1 << 1)   /* Max segment size available */
#define VIRTIO_BLK_F_SEG_MAX        (1 << 2)   /* Max segments available */
#define VIRTIO_BLK_F_GEOMETRY       (1 << 4)   /* Geometry available */
#define VIRTIO_BLK_F_RO             (1 << 5)   /* Device is read-only */
#define VIRTIO_BLK_F_BLK_SIZE       (1 << 6)   /* Block size available */
#define VIRTIO_BLK_F_FLUSH          (1 << 9)   /* Flush command supported */

/*
 * Virtio block request types
 */
#define VIRTIO_BLK_T_IN             0   /* Read */
#define VIRTIO_BLK_T_OUT            1   /* Write */
#define VIRTIO_BLK_T_FLUSH          4   /* Flush */

/*
 * Virtio block status values
 */
#define VIRTIO_BLK_S_OK             0
#define VIRTIO_BLK_S_IOERR          1
#define VIRTIO_BLK_S_UNSUPP         2

/*
 * Virtio block request header
 */
typedef struct virtio_blk_req_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) virtio_blk_req_header_t;

/*
 * Virtio block device private data
 */
typedef struct virtio_blk_dev {
    virtio_device_t *vdev;
    block_device_t blk_dev;
    virtqueue_t *vq;
    uint64_t capacity;
    uint32_t blk_size;
    bool read_only;
} virtio_blk_dev_t;

/*
 * Request cookie
 */
typedef struct virtio_blk_request {
    virtio_blk_req_header_t header;
    uint8_t status;
    volatile bool complete;
} virtio_blk_request_t;

/* Device index counter */
static int virtio_blk_index = 0;

/*
 * Synchronous read/write helper
 */
static int virtio_blk_rw(virtio_blk_dev_t *vblk, uint64_t sector,
                         uint32_t count, void *buf, bool write) {
    virtqueue_t *vq = vblk->vq;

    /* Allocate request */
    virtio_blk_request_t *req = kzalloc(sizeof(virtio_blk_request_t));
    if (!req) {
        return -12;  /* ENOMEM */
    }

    /* Set up header */
    req->header.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->header.reserved = 0;
    req->header.sector = sector;
    req->status = 0xFF;  /* Invalid initial value */
    req->complete = false;

    /* Set up scatter-gather */
    void *sg_bufs[3];
    uint32_t sg_lens[3];
    uint32_t out_num, in_num;

    sg_bufs[0] = &req->header;
    sg_lens[0] = sizeof(req->header);

    sg_bufs[1] = buf;
    sg_lens[1] = count * vblk->blk_size;

    sg_bufs[2] = &req->status;
    sg_lens[2] = 1;

    if (write) {
        out_num = 2;  /* header + data */
        in_num = 1;   /* status */
    } else {
        out_num = 1;  /* header only */
        in_num = 2;   /* data + status */
    }

    /* Disable interrupts during request */
    virtqueue_disable_irq(vq);

    /* Add to virtqueue */
    if (virtqueue_add(vq, sg_bufs, sg_lens, out_num, in_num, req) != 0) {
        kfree(req);
        return -16;  /* EBUSY */
    }

    /* Kick the device */
    virtqueue_kick(vblk->vdev, vq);

    /* Poll for completion */
    int timeout = 1000000;  /* ~1 second at CPU speed */
    while (!virtqueue_has_data(vq) && timeout > 0) {
        __asm__ volatile("pause");
        timeout--;
    }

    if (timeout <= 0) {
        ERROR("virtio-blk: request timeout");
        kfree(req);
        return -5;  /* EIO */
    }

    /* Get completion */
    uint32_t len;
    virtio_blk_request_t *done = virtqueue_get(vq, &len);
    (void)done;  /* Should be same as req */

    /* Check status */
    int result = 0;
    if (req->status != VIRTIO_BLK_S_OK) {
        ERROR("virtio-blk: request failed with status %d", req->status);
        result = -5;  /* EIO */
    }

    kfree(req);
    return result;
}

/*
 * Block device read operation
 */
static int virtio_blk_read(block_device_t *dev, uint64_t sector,
                           uint32_t count, void *buf) {
    virtio_blk_dev_t *vblk = dev->private;
    return virtio_blk_rw(vblk, sector, count, buf, false);
}

/*
 * Block device write operation
 */
static int virtio_blk_write(block_device_t *dev, uint64_t sector,
                            uint32_t count, const void *buf) {
    virtio_blk_dev_t *vblk = dev->private;
    return virtio_blk_rw(vblk, sector, count, (void *)buf, true);
}

/*
 * Block device flush operation
 */
static int virtio_blk_flush(block_device_t *dev) {
    virtio_blk_dev_t *vblk = dev->private;

    /* Check if flush is supported */
    if (!(vblk->vdev->features & VIRTIO_BLK_F_FLUSH)) {
        return 0;  /* No flush needed */
    }

    /* TODO: implement flush command */
    return 0;
}

/* Block operations */
static block_ops_t virtio_blk_ops = {
    .read = virtio_blk_read,
    .write = virtio_blk_write,
    .flush = virtio_blk_flush,
};

/*
 * Virtio block interrupt handler
 * Note: Currently unused as driver uses polling mode
 */
static void __attribute__((unused)) virtio_blk_irq_handler(void *ctx) {
    virtio_blk_dev_t *vblk = ctx;
    if (!vblk || !vblk->vdev) return;

    /* Read and clear ISR */
    uint8_t isr = virtio_read_isr(vblk->vdev);
    if (isr == 0) return;

    DEBUG("virtio-blk IRQ: isr=0x%02x", isr);

    /* Process completed requests */
    while (virtqueue_has_data(vblk->vq)) {
        uint32_t len;
        void *cookie = virtqueue_get(vblk->vq, &len);
        if (cookie) {
            virtio_blk_request_t *req = cookie;
            req->complete = true;
        }
    }
}

/*
 * Probe and initialize virtio block device
 */
void virtio_blk_probe(virtio_device_t *vdev) {
    INFO("Probing virtio-blk device at %02x:%02x.%x",
         vdev->bus, vdev->slot, vdev->func);

    /* Allocate device structure */
    virtio_blk_dev_t *vblk = kzalloc(sizeof(virtio_blk_dev_t));
    if (!vblk) {
        ERROR("Failed to allocate virtio-blk device");
        virtio_set_status(vdev, VIRTIO_STATUS_FAILED);
        return;
    }

    vblk->vdev = vdev;
    vdev->private = vblk;

    /* Negotiate features */
    uint64_t wanted = VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH;
    uint64_t features = virtio_negotiate_features(vdev, wanted);

    /* Read configuration */
    uint64_t capacity_lo = virtio_read32(vdev, VIRTIO_PCI_CONFIG + VIRTIO_BLK_CFG_CAPACITY);
    uint64_t capacity_hi = virtio_read32(vdev, VIRTIO_PCI_CONFIG + VIRTIO_BLK_CFG_CAPACITY + 4);
    vblk->capacity = (capacity_hi << 32) | capacity_lo;

    if (features & VIRTIO_BLK_F_BLK_SIZE) {
        vblk->blk_size = virtio_read32(vdev, VIRTIO_PCI_CONFIG + VIRTIO_BLK_CFG_BLK_SIZE);
    } else {
        vblk->blk_size = 512;
    }

    vblk->read_only = (features & VIRTIO_BLK_F_RO) != 0;

    INFO("virtio-blk: capacity=%llu sectors, blk_size=%u, features=0x%llx%s",
         vblk->capacity, vblk->blk_size, features,
         vblk->read_only ? " (read-only)" : "");

    /* Initialize virtqueue */
    vblk->vq = virtqueue_init(vdev, 0);
    if (!vblk->vq) {
        ERROR("Failed to initialize virtio-blk virtqueue");
        virtio_set_status(vdev, VIRTIO_STATUS_FAILED);
        kfree(vblk);
        return;
    }
    vblk->vq->notify_offset = 0;

    /* Mark device ready */
    uint8_t status = virtio_get_status(vdev);
    virtio_set_status(vdev, status | VIRTIO_STATUS_DRIVER_OK);

    /* Register IRQ handler if available */
    if (vdev->irq > 0 && vdev->irq < 16) {
        /* Register with IRQ offset for PIC */
        DEBUG("virtio-blk: registering IRQ %d", vdev->irq);
        /* Note: Would need proper IRQ registration here */
    }

    /* Set up block device */
    int idx = virtio_blk_index++;
    if (idx == 0) {
        strcpy(vblk->blk_dev.name, "vda");
    } else {
        vblk->blk_dev.name[0] = 'v';
        vblk->blk_dev.name[1] = 'd';
        vblk->blk_dev.name[2] = 'a' + (idx % 26);
        vblk->blk_dev.name[3] = '\0';
    }

    vblk->blk_dev.sector_count = vblk->capacity;
    vblk->blk_dev.sector_size = vblk->blk_size;
    vblk->blk_dev.capacity = vblk->capacity * vblk->blk_size;
    vblk->blk_dev.read_only = vblk->read_only;
    vblk->blk_dev.removable = false;
    vblk->blk_dev.ops = &virtio_blk_ops;
    vblk->blk_dev.private = vblk;

    /* Register block device */
    if (block_register(&vblk->blk_dev) != 0) {
        ERROR("Failed to register block device %s", vblk->blk_dev.name);
        virtqueue_destroy(vblk->vq);
        virtio_set_status(vdev, VIRTIO_STATUS_FAILED);
        kfree(vblk);
        return;
    }

    INFO("Registered virtio-blk device: %s (%llu MB)",
         vblk->blk_dev.name, vblk->blk_dev.capacity / (1024 * 1024));
}
