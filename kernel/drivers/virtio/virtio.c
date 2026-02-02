/* virtio.c - Virtio Device Core Implementation */

#include "virtio.h"
#include "../pci.h"
#include "../../debug/debug.h"
#include "../../mm/pmm.h"
#include "../../mm/slab.h"
#include "../../lib/string.h"

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/*
 * Read 8-bit virtio register
 */
uint8_t virtio_read8(virtio_device_t *dev, uint16_t offset) {
    return inb(dev->io_base + offset);
}

/*
 * Read 16-bit virtio register
 */
uint16_t virtio_read16(virtio_device_t *dev, uint16_t offset) {
    return inw(dev->io_base + offset);
}

/*
 * Read 32-bit virtio register
 */
uint32_t virtio_read32(virtio_device_t *dev, uint16_t offset) {
    return inl(dev->io_base + offset);
}

/*
 * Write 8-bit virtio register
 */
void virtio_write8(virtio_device_t *dev, uint16_t offset, uint8_t val) {
    outb(dev->io_base + offset, val);
}

/*
 * Write 16-bit virtio register
 */
void virtio_write16(virtio_device_t *dev, uint16_t offset, uint16_t val) {
    outw(dev->io_base + offset, val);
}

/*
 * Write 32-bit virtio register
 */
void virtio_write32(virtio_device_t *dev, uint16_t offset, uint32_t val) {
    outl(dev->io_base + offset, val);
}

/*
 * Reset virtio device
 */
void virtio_reset(virtio_device_t *dev) {
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_RESET);
}

/*
 * Set device status
 */
void virtio_set_status(virtio_device_t *dev, uint8_t status) {
    virtio_write8(dev, VIRTIO_PCI_STATUS, status);
}

/*
 * Get device status
 */
uint8_t virtio_get_status(virtio_device_t *dev) {
    return virtio_read8(dev, VIRTIO_PCI_STATUS);
}

/*
 * Read ISR status (and clear it)
 */
uint8_t virtio_read_isr(virtio_device_t *dev) {
    return virtio_read8(dev, VIRTIO_PCI_ISR);
}

/*
 * Negotiate features
 */
uint64_t virtio_negotiate_features(virtio_device_t *dev, uint64_t requested) {
    /* Read host features */
    uint32_t host_features = virtio_read32(dev, VIRTIO_PCI_HOST_FEATURES);

    /* Negotiate (intersection of host and requested) */
    uint32_t negotiated = host_features & (uint32_t)requested;

    /* Write guest features */
    virtio_write32(dev, VIRTIO_PCI_GUEST_FEATURES, negotiated);

    dev->features = negotiated;
    return negotiated;
}

/*
 * Calculate vring size requirements
 */
static size_t vring_size(uint16_t num) {
    size_t desc_size = num * sizeof(vring_desc_t);
    size_t avail_size = sizeof(vring_avail_t) + num * sizeof(uint16_t) + sizeof(uint16_t);
    size_t used_size = sizeof(vring_used_t) + num * sizeof(vring_used_elem_t) + sizeof(uint16_t);

    /* Align avail to 2, used to 4096 */
    size_t size = desc_size;
    size = (size + 1) & ~1UL;
    size += avail_size;
    size = (size + 4095) & ~4095UL;
    size += used_size;

    return size;
}

/*
 * Initialize a virtqueue
 */
virtqueue_t *virtqueue_init(virtio_device_t *dev, uint16_t index) {
    /* Select queue */
    virtio_write16(dev, VIRTIO_PCI_QUEUE_SEL, index);

    /* Get queue size */
    uint16_t size = virtio_read16(dev, VIRTIO_PCI_QUEUE_SIZE);
    if (size == 0) {
        DEBUG("virtqueue %d has size 0, not present", index);
        return NULL;
    }

    DEBUG("virtqueue %d: size=%u", index, size);

    /* Allocate virtqueue structure */
    virtqueue_t *vq = kzalloc(sizeof(virtqueue_t));
    if (!vq) {
        ERROR("Failed to allocate virtqueue");
        return NULL;
    }

    vq->size = size;
    vq->free_head = 0;
    vq->num_free = size;
    vq->last_used_idx = 0;

    /* Allocate ring memory (must be physically contiguous and page-aligned) */
    vq->ring_size = vring_size(size);
    uint64_t num_pages = (vq->ring_size + PAGE_SIZE - 1) / PAGE_SIZE;

    vq->ring_phys = alloc_pages(num_pages);
    if (!vq->ring_phys) {
        ERROR("Failed to allocate virtqueue ring memory");
        kfree(vq);
        return NULL;
    }

    /* Get virtual address (assuming identity mapping for low memory) */
    vq->ring_mem = (void*)vq->ring_phys;
    memset(vq->ring_mem, 0, vq->ring_size);

    /* Set up ring pointers */
    vq->desc = (vring_desc_t *)vq->ring_mem;

    size_t avail_offset = size * sizeof(vring_desc_t);
    avail_offset = (avail_offset + 1) & ~1UL;
    vq->avail = (vring_avail_t *)((uint8_t *)vq->ring_mem + avail_offset);

    size_t used_offset = avail_offset + sizeof(vring_avail_t) + size * sizeof(uint16_t) + sizeof(uint16_t);
    used_offset = (used_offset + 4095) & ~4095UL;
    vq->used = (vring_used_t *)((uint8_t *)vq->ring_mem + used_offset);

    /* Allocate callback array */
    vq->callbacks = kzalloc(size * sizeof(void *));
    if (!vq->callbacks) {
        ERROR("Failed to allocate virtqueue callbacks");
        free_pages(vq->ring_phys, num_pages);
        kfree(vq);
        return NULL;
    }

    /* Set up free list */
    for (uint16_t i = 0; i < size - 1; i++) {
        vq->desc[i].next = i + 1;
    }

    /* Tell device where the queue is */
    uint32_t pfn = vq->ring_phys / PAGE_SIZE;
    virtio_write32(dev, VIRTIO_PCI_QUEUE_PFN, pfn);

    DEBUG("virtqueue %d initialized: desc=%p avail=%p used=%p",
          index, vq->desc, vq->avail, vq->used);

    return vq;
}

/*
 * Add buffer to virtqueue
 */
int virtqueue_add(virtqueue_t *vq, void **sg_bufs, uint32_t *sg_lens,
                  uint32_t out_num, uint32_t in_num, void *cookie) {
    uint32_t total = out_num + in_num;

    if (total == 0 || total > vq->num_free) {
        return -1;
    }

    uint16_t head = vq->free_head;
    uint16_t idx = head;

    /* Add descriptors */
    for (uint32_t i = 0; i < total; i++) {
        vq->desc[idx].addr = (uint64_t)(uintptr_t)sg_bufs[i];
        vq->desc[idx].len = sg_lens[i];

        /* Set flags */
        vq->desc[idx].flags = 0;
        if (i >= out_num) {
            vq->desc[idx].flags |= VRING_DESC_F_WRITE;
        }
        if (i < total - 1) {
            vq->desc[idx].flags |= VRING_DESC_F_NEXT;
        }

        idx = vq->desc[idx].next;
    }

    /* Update free list */
    vq->free_head = idx;
    vq->num_free -= total;

    /* Save cookie */
    vq->callbacks[head] = cookie;

    /* Add to available ring */
    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->size] = head;
    __asm__ volatile("mfence" ::: "memory");
    vq->avail->idx = avail_idx + 1;

    return 0;
}

/*
 * Notify device
 */
void virtqueue_kick(virtio_device_t *dev, virtqueue_t *vq) {
    __asm__ volatile("mfence" ::: "memory");
    virtio_write16(dev, VIRTIO_PCI_QUEUE_NOTIFY, vq->notify_offset);
}

/*
 * Get completed buffer
 */
void *virtqueue_get(virtqueue_t *vq, uint32_t *len) {
    if (vq->last_used_idx == vq->used->idx) {
        return NULL;  /* No completed buffers */
    }

    __asm__ volatile("lfence" ::: "memory");

    uint16_t used_idx = vq->last_used_idx % vq->size;
    vring_used_elem_t *elem = &vq->used->ring[used_idx];

    uint16_t head = elem->id;
    if (len) {
        *len = elem->len;
    }

    /* Return descriptors to free list */
    uint16_t idx = head;
    uint32_t count = 0;
    while (vq->desc[idx].flags & VRING_DESC_F_NEXT) {
        idx = vq->desc[idx].next;
        count++;
    }
    vq->desc[idx].next = vq->free_head;
    vq->free_head = head;
    vq->num_free += count + 1;

    vq->last_used_idx++;

    return vq->callbacks[head];
}

/*
 * Check if virtqueue has data
 */
bool virtqueue_has_data(virtqueue_t *vq) {
    __asm__ volatile("lfence" ::: "memory");
    return vq->last_used_idx != vq->used->idx;
}

/*
 * Enable interrupts
 */
void virtqueue_enable_irq(virtqueue_t *vq) {
    vq->avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
}

/*
 * Disable interrupts
 */
void virtqueue_disable_irq(virtqueue_t *vq) {
    vq->avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
}

/*
 * Destroy virtqueue
 */
void virtqueue_destroy(virtqueue_t *vq) {
    if (!vq) return;

    if (vq->callbacks) {
        kfree(vq->callbacks);
    }

    if (vq->ring_phys) {
        uint64_t num_pages = (vq->ring_size + PAGE_SIZE - 1) / PAGE_SIZE;
        free_pages(vq->ring_phys, num_pages);
    }

    kfree(vq);
}

/*
 * Probe callback for virtio-blk devices
 */
extern void virtio_blk_probe(virtio_device_t *dev);
extern void virtio_net_probe(virtio_device_t *dev);

/*
 * Scan for virtio devices
 */
static void virtio_pci_callback(pci_device_t *pci_dev, void *ctx) {
    (void)ctx;

    /* Check for virtio vendor */
    if (pci_dev->vendor_id != VIRTIO_PCI_VENDOR_ID) {
        return;
    }

    /* Check device ID range */
    if (pci_dev->device_id < VIRTIO_PCI_DEVICE_ID_MIN ||
        pci_dev->device_id > VIRTIO_PCI_DEVICE_ID_MAX) {
        return;
    }

    /* Get device type */
    uint8_t device_type = pci_dev->device_id - VIRTIO_PCI_DEVICE_ID_MIN;

    INFO("Found virtio device: type=%d at %02x:%02x.%x",
         device_type, pci_dev->bus, pci_dev->slot, pci_dev->func);

    /* Get I/O base from BAR0 */
    if (!pci_dev->bar_is_io[0]) {
        WARN("Virtio device has no I/O BAR, skipping");
        return;
    }

    uint16_t io_base = pci_get_bar_addr(pci_dev, 0) & 0xFFFF;
    DEBUG("  I/O base: 0x%04x", io_base);

    /* Enable I/O space and bus mastering */
    pci_enable_io_space(pci_dev->bus, pci_dev->slot, pci_dev->func);
    pci_enable_bus_master(pci_dev->bus, pci_dev->slot, pci_dev->func);

    /* Create virtio device structure */
    virtio_device_t *vdev = kzalloc(sizeof(virtio_device_t));
    if (!vdev) {
        ERROR("Failed to allocate virtio device");
        return;
    }

    vdev->bus = pci_dev->bus;
    vdev->slot = pci_dev->slot;
    vdev->func = pci_dev->func;
    vdev->device_id = pci_dev->device_id;
    vdev->device_type = device_type;
    vdev->io_base = io_base;
    vdev->irq = pci_dev->irq;

    /* Initialize device */
    virtio_reset(vdev);
    virtio_set_status(vdev, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_set_status(vdev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* Probe device-specific driver */
    switch (device_type) {
        case VIRTIO_DEV_NET:
            virtio_net_probe(vdev);
            break;
        case VIRTIO_DEV_BLOCK:
            virtio_blk_probe(vdev);
            break;
        default:
            DEBUG("No driver for virtio device type %d", device_type);
            kfree(vdev);
            break;
    }
}

/*
 * Initialize virtio subsystem
 */
void virtio_init(void) {
    INFO("Initializing virtio subsystem");

    /* Scan PCI bus for virtio devices */
    pci_enumerate(virtio_pci_callback, NULL);

    INFO("Virtio scan complete");
}
