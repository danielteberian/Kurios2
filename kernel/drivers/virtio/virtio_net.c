/* virtio_net.c - Virtio Network Device Driver */

#include "virtio_net.h"
#include "virtio.h"
#include "../../mm/slab.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../debug/debug.h"
#include "../../lib/string.h"
#include "../../sync/spinlock.h"
#include "../../net/netdev.h"

/* Maximum packet size (MTU + headers) */
#define MAX_PACKET_SIZE 1514

/* Virtio-net IRQ handler */
__attribute__((unused))
static void virtio_net_irq_handler(void *state) {
    virtio_net_dev_t *dev = (virtio_net_dev_t *)state;
    if (!dev || !dev->vdev) {
        return;
    }

    /* Read and acknowledge interrupt */
    uint8_t isr = virtio_read_isr(dev->vdev);
    if (!(isr & 0x1)) {
        return;  /* Not our interrupt */
    }

    /* Process TX completions */
    while (1) {
        uint32_t len = 0;
        void *buf = virtqueue_get(dev->tx_vq, &len);
        if (!buf) break;

        /* Free the transmitted buffer */
        kfree(buf);
        dev->tx_packets++;
    }

    /* Process RX packets */
    while (1) {
        uint32_t len = 0;
        void *buf = virtqueue_get(dev->rx_vq, &len);
        if (!buf) break;

        if (len > sizeof(virtio_net_hdr_t)) {
            /* Skip virtio-net header and deliver packet */
            uint8_t *packet_data = (uint8_t *)buf + sizeof(virtio_net_hdr_t);
            uint32_t packet_len = len - sizeof(virtio_net_hdr_t);

            /* Deliver to network stack */
            packet_t *pkt = packet_alloc(packet_len);
            if (pkt) {
                memcpy(pkt->data, packet_data, packet_len);
                pkt->len = packet_len;
                netdev_receive(&dev->netdev, pkt);
            }

            dev->rx_packets++;
            dev->rx_bytes += packet_len;
        }

        /* Reuse RX buffer */
        void *sg_bufs[] = {buf};
        uint32_t sg_lens[] = {MAX_PACKET_SIZE};
        virtqueue_add(dev->rx_vq, sg_bufs, sg_lens, 0, 1, buf);
    }

    /* Kick RX queue to refill */
    virtqueue_kick(dev->vdev, dev->rx_vq);
}

/* Transmit packet */
static int virtio_net_transmit(netdev_t *netdev, packet_t *pkt) {
    virtio_net_dev_t *dev = (virtio_net_dev_t *)netdev;

    if (!pkt || pkt->len == 0 || pkt->len > MAX_PACKET_SIZE) {
        return -1;
    }

    uint64_t flags = spin_lock_irqsave(&dev->tx_lock);

    /* Allocate buffer for virtio-net header + packet */
    uint32_t total_len = sizeof(virtio_net_hdr_t) + pkt->len;
    void *buf = kmalloc(total_len);
    if (!buf) {
        spin_unlock_irqrestore(&dev->tx_lock, flags);
        return -1;
    }

    /* Build virtio-net header (all zeros for simple packets) */
    virtio_net_hdr_t *hdr = (virtio_net_hdr_t *)buf;
    memset(hdr, 0, sizeof(virtio_net_hdr_t));

    /* Copy packet data */
    memcpy((uint8_t *)buf + sizeof(virtio_net_hdr_t), pkt->data, pkt->len);

    /* Add to TX queue */
    void *sg_bufs[] = {buf};
    uint32_t sg_lens[] = {total_len};
    if (virtqueue_add(dev->tx_vq, sg_bufs, sg_lens, 1, 0, buf) < 0) {
        kfree(buf);
        spin_unlock_irqrestore(&dev->tx_lock, flags);
        return -1;
    }

    /* Kick device */
    virtqueue_kick(dev->vdev, dev->tx_vq);

    dev->tx_bytes += pkt->len;

    spin_unlock_irqrestore(&dev->tx_lock, flags);

    return 0;
}

/* Network device operations */
static netdev_ops_t virtio_net_ops = {
    .open = NULL,
    .close = NULL,
    .transmit = virtio_net_transmit,
};

/* Allocate and setup RX buffers */
static int virtio_net_setup_rx_buffers(virtio_net_dev_t *dev) {
    for (int i = 0; i < VIRTIO_NET_RX_BUFS; i++) {
        /* Allocate buffer for header + max packet */
        void *buf = kmalloc(MAX_PACKET_SIZE);
        if (!buf) {
            ERROR("virtio-net: Failed to allocate RX buffer %d", i);
            return -1;
        }

        dev->rx_bufs[i] = buf;

        /* Add to RX virtqueue */
        void *sg_bufs[] = {buf};
        uint32_t sg_lens[] = {MAX_PACKET_SIZE};
        if (virtqueue_add(dev->rx_vq, sg_bufs, sg_lens, 0, 1, buf) < 0) {
            ERROR("virtio-net: Failed to add RX buffer %d to queue", i);
            kfree(buf);
            return -1;
        }
    }

    /* Kick RX queue */
    virtqueue_kick(dev->vdev, dev->rx_vq);

    DEBUG("virtio-net: %d RX buffers allocated", VIRTIO_NET_RX_BUFS);
    return 0;
}

/* Probe virtio-net device */
void virtio_net_probe(virtio_device_t *vdev) {
    INFO("Probing virtio-net device...");

    if (!vdev) {
        ERROR("virtio-net: Invalid device");
        return;
    }

    /* Allocate device structure */
    virtio_net_dev_t *dev = kmalloc(sizeof(virtio_net_dev_t));
    if (!dev) {
        ERROR("virtio-net: Failed to allocate device structure");
        return;
    }

    memset(dev, 0, sizeof(virtio_net_dev_t));
    dev->vdev = vdev;
    spin_init(&dev->tx_lock);

    /* Reset device */
    virtio_reset(vdev);

    /* Acknowledge device */
    virtio_set_status(vdev, VIRTIO_STATUS_ACKNOWLEDGE);

    /* Set driver status */
    virtio_set_status(vdev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* Negotiate features */
    uint64_t requested = VIRTIO_NET_F_MAC | VIRTIO_NET_F_STATUS;
    uint64_t negotiated = virtio_negotiate_features(vdev, requested);

    DEBUG("virtio-net: Negotiated features: 0x%llx", negotiated);

    /* Read MAC address from config space (if supported) */
    if (negotiated & VIRTIO_NET_F_MAC) {
        for (int i = 0; i < 6; i++) {
            dev->mac[i] = virtio_read8(vdev, VIRTIO_PCI_CONFIG + i);
        }
        INFO("virtio-net: MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
             dev->mac[0], dev->mac[1], dev->mac[2],
             dev->mac[3], dev->mac[4], dev->mac[5]);
    } else {
        /* Generate a random MAC address */
        dev->mac[0] = 0x52;  /* Locally administered */
        dev->mac[1] = 0x54;
        dev->mac[2] = 0x00;
        dev->mac[3] = 0x12;
        dev->mac[4] = 0x34;
        dev->mac[5] = 0x56;
        INFO("virtio-net: Using default MAC address: 52:54:00:12:34:56");
    }

    /* Initialize virtqueues */
    /* Queue 0: RX */
    dev->rx_vq = virtqueue_init(vdev, 0);
    if (!dev->rx_vq) {
        ERROR("virtio-net: Failed to create RX virtqueue");
        kfree(dev);
        return;
    }

    /* Queue 1: TX */
    dev->tx_vq = virtqueue_init(vdev, 1);
    if (!dev->tx_vq) {
        ERROR("virtio-net: Failed to create TX virtqueue");
        virtqueue_destroy(dev->rx_vq);
        kfree(dev);
        return;
    }

    DEBUG("virtio-net: Virtqueues created (RX: %p, TX: %p)", dev->rx_vq, dev->tx_vq);

    /* Setup RX buffers */
    if (virtio_net_setup_rx_buffers(dev) < 0) {
        ERROR("virtio-net: Failed to setup RX buffers");
        virtqueue_destroy(dev->rx_vq);
        virtqueue_destroy(dev->tx_vq);
        kfree(dev);
        return;
    }

    /* Set DRIVER_OK status */
    virtio_set_status(vdev, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    /* Initialize network device structure */
    dev->netdev.ops = &virtio_net_ops;
    dev->netdev.mtu = 1500;
    dev->netdev.flags = NETDEV_UP | NETDEV_RUNNING;
    memcpy(dev->netdev.mac, dev->mac, 6);
    snprintf(dev->netdev.name, sizeof(dev->netdev.name), "eth0");

    /* Assign IP address (for now, use a simple static assignment) */
    /* In a real system, this would be done by DHCP or user configuration */
    dev->netdev.ip = (10 << 0) | (0 << 8) | (2 << 16) | (15 << 24);  /* 10.0.2.15 */
    dev->netdev.netmask = (255 << 0) | (255 << 8) | (255 << 16) | (0 << 24);  /* 255.255.255.0 */

    /* Register with network subsystem */
    if (netdev_register(&dev->netdev) < 0) {
        ERROR("virtio-net: Failed to register network device");
        virtqueue_destroy(dev->rx_vq);
        virtqueue_destroy(dev->tx_vq);
        kfree(dev);
        return;
    }

    INFO("virtio-net: Device initialized successfully (IP: 10.0.2.15)");
}
