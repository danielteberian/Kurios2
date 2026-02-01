/* virtio.h - Virtio Device Interface */
#ifndef _KERNEL_VIRTIO_H
#define _KERNEL_VIRTIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Virtio PCI device IDs (with transitional support)
 */
#define VIRTIO_PCI_VENDOR_ID        0x1AF4
#define VIRTIO_PCI_DEVICE_ID_MIN    0x1000
#define VIRTIO_PCI_DEVICE_ID_MAX    0x103F

/* Specific device types (device ID - 0x1000 = device type) */
#define VIRTIO_DEV_NET              1
#define VIRTIO_DEV_BLOCK            2
#define VIRTIO_DEV_CONSOLE          3
#define VIRTIO_DEV_RNG              4
#define VIRTIO_DEV_BALLOON          5
#define VIRTIO_DEV_SCSI             8
#define VIRTIO_DEV_9P               9
#define VIRTIO_DEV_GPU              16

/*
 * Virtio device status bits
 */
#define VIRTIO_STATUS_RESET         0x00
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01    /* OS has found the device */
#define VIRTIO_STATUS_DRIVER        0x02    /* OS knows how to drive the device */
#define VIRTIO_STATUS_DRIVER_OK     0x04    /* Driver is set up and ready */
#define VIRTIO_STATUS_FEATURES_OK   0x08    /* Feature negotiation complete */
#define VIRTIO_STATUS_NEEDS_RESET   0x40    /* Device needs reset */
#define VIRTIO_STATUS_FAILED        0x80    /* Something went wrong */

/*
 * Common feature bits
 */
#define VIRTIO_F_RING_INDIRECT_DESC (1ULL << 28)  /* Indirect descriptors */
#define VIRTIO_F_RING_EVENT_IDX     (1ULL << 29)  /* Event index suppression */
#define VIRTIO_F_VERSION_1          (1ULL << 32)  /* Virtio 1.0 compliant */

/*
 * Legacy PCI virtio registers (BAR0)
 * Used for legacy/transitional devices
 */
#define VIRTIO_PCI_HOST_FEATURES    0x00    /* 32-bit: Host features */
#define VIRTIO_PCI_GUEST_FEATURES   0x04    /* 32-bit: Guest features */
#define VIRTIO_PCI_QUEUE_PFN        0x08    /* 32-bit: Queue physical page */
#define VIRTIO_PCI_QUEUE_SIZE       0x0C    /* 16-bit: Queue size */
#define VIRTIO_PCI_QUEUE_SEL        0x0E    /* 16-bit: Queue selector */
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10    /* 16-bit: Queue notify */
#define VIRTIO_PCI_STATUS           0x12    /* 8-bit: Device status */
#define VIRTIO_PCI_ISR              0x13    /* 8-bit: ISR status */
#define VIRTIO_PCI_CONFIG           0x14    /* Device-specific config starts */

/*
 * Virtqueue descriptor flags
 */
#define VRING_DESC_F_NEXT           0x01    /* Buffer continues via next field */
#define VRING_DESC_F_WRITE          0x02    /* Buffer is device-writable */
#define VRING_DESC_F_INDIRECT       0x04    /* Buffer contains list of descriptors */

/*
 * Virtqueue available ring flags
 */
#define VRING_AVAIL_F_NO_INTERRUPT  0x01    /* Suppress interrupts */

/*
 * Virtqueue used ring flags
 */
#define VRING_USED_F_NO_NOTIFY      0x01    /* Suppress notifications */

/*
 * Virtqueue descriptor (16 bytes)
 */
typedef struct vring_desc {
    uint64_t addr;      /* Physical address of buffer */
    uint32_t len;       /* Length of buffer */
    uint16_t flags;     /* VRING_DESC_F_* flags */
    uint16_t next;      /* Index of next descriptor if F_NEXT */
} __attribute__((packed)) vring_desc_t;

/*
 * Virtqueue available ring
 */
typedef struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];    /* Variable size: queue_size entries */
    /* uint16_t used_event; // At end if EVENT_IDX */
} __attribute__((packed)) vring_avail_t;

/*
 * Virtqueue used ring element
 */
typedef struct vring_used_elem {
    uint32_t id;        /* Index of descriptor chain start */
    uint32_t len;       /* Number of bytes written to device-writable buffers */
} __attribute__((packed)) vring_used_elem_t;

/*
 * Virtqueue used ring
 */
typedef struct vring_used {
    uint16_t flags;
    uint16_t idx;
    vring_used_elem_t ring[];  /* Variable size: queue_size entries */
    /* uint16_t avail_event; // At end if EVENT_IDX */
} __attribute__((packed)) vring_used_t;

/*
 * Complete virtqueue structure
 */
typedef struct virtqueue {
    uint16_t size;              /* Queue size (power of 2) */
    uint16_t free_head;         /* Index of first free descriptor */
    uint16_t num_free;          /* Number of free descriptors */
    uint16_t last_used_idx;     /* Last seen used index */

    /* Ring pointers (physical addresses) */
    vring_desc_t *desc;
    vring_avail_t *avail;
    vring_used_t *used;

    /* Memory region for the ring */
    void *ring_mem;
    uint64_t ring_phys;
    size_t ring_size;

    /* Callback data for pending requests */
    void **callbacks;

    /* Notify register offset (device-specific) */
    uint16_t notify_offset;
} virtqueue_t;

/*
 * Virtio device structure
 */
typedef struct virtio_device {
    /* PCI location */
    uint8_t bus;
    uint8_t slot;
    uint8_t func;

    /* Device info */
    uint16_t device_id;
    uint8_t device_type;

    /* I/O base (from BAR0) */
    uint16_t io_base;

    /* Memory base (from BAR) */
    uint64_t mmio_base;

    /* Features */
    uint64_t features;

    /* Virtqueues */
    uint16_t num_queues;
    virtqueue_t *queues;

    /* IRQ */
    uint8_t irq;

    /* Driver private data */
    void *private;
} virtio_device_t;

/*
 * Initialize virtio subsystem and scan for devices
 */
void virtio_init(void);

/*
 * Read 8-bit virtio config register
 */
uint8_t virtio_read8(virtio_device_t *dev, uint16_t offset);

/*
 * Read 16-bit virtio config register
 */
uint16_t virtio_read16(virtio_device_t *dev, uint16_t offset);

/*
 * Read 32-bit virtio config register
 */
uint32_t virtio_read32(virtio_device_t *dev, uint16_t offset);

/*
 * Write 8-bit virtio config register
 */
void virtio_write8(virtio_device_t *dev, uint16_t offset, uint8_t val);

/*
 * Write 16-bit virtio config register
 */
void virtio_write16(virtio_device_t *dev, uint16_t offset, uint16_t val);

/*
 * Write 32-bit virtio config register
 */
void virtio_write32(virtio_device_t *dev, uint16_t offset, uint32_t val);

/*
 * Reset virtio device
 */
void virtio_reset(virtio_device_t *dev);

/*
 * Set device status
 */
void virtio_set_status(virtio_device_t *dev, uint8_t status);

/*
 * Get device status
 */
uint8_t virtio_get_status(virtio_device_t *dev);

/*
 * Negotiate features with device
 *
 * @param dev       Device
 * @param requested Features the driver wants
 * @return Negotiated features (intersection of host and requested)
 */
uint64_t virtio_negotiate_features(virtio_device_t *dev, uint64_t requested);

/*
 * Initialize a virtqueue
 *
 * @param dev    Device
 * @param index  Queue index
 * @return Virtqueue pointer or NULL on failure
 */
virtqueue_t *virtqueue_init(virtio_device_t *dev, uint16_t index);

/*
 * Add buffer to virtqueue
 *
 * @param vq      Virtqueue
 * @param sg      Scatter-gather list (addr, len pairs)
 * @param out_num Number of device-readable buffers
 * @param in_num  Number of device-writable buffers
 * @param cookie  Cookie to return with completion
 * @return 0 on success, negative error on failure
 */
int virtqueue_add(virtqueue_t *vq, void **sg_bufs, uint32_t *sg_lens,
                  uint32_t out_num, uint32_t in_num, void *cookie);

/*
 * Notify device that buffers are available
 */
void virtqueue_kick(virtio_device_t *dev, virtqueue_t *vq);

/*
 * Get completed buffer from virtqueue
 *
 * @param vq  Virtqueue
 * @param len Output: bytes written to device-writable buffers
 * @return Cookie from virtqueue_add, or NULL if no completed buffers
 */
void *virtqueue_get(virtqueue_t *vq, uint32_t *len);

/*
 * Check if virtqueue has pending completions
 */
bool virtqueue_has_data(virtqueue_t *vq);

/*
 * Free virtqueue resources
 */
void virtqueue_destroy(virtqueue_t *vq);

/*
 * Enable interrupts for a virtqueue
 */
void virtqueue_enable_irq(virtqueue_t *vq);

/*
 * Disable interrupts for a virtqueue
 */
void virtqueue_disable_irq(virtqueue_t *vq);

/*
 * Read ISR status (and clear it)
 */
uint8_t virtio_read_isr(virtio_device_t *dev);

#endif /* _KERNEL_VIRTIO_H */
