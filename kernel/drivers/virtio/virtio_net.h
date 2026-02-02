/* virtio_net.h - Virtio Network Device Driver */
#ifndef _DRIVERS_VIRTIO_VIRTIO_NET_H
#define _DRIVERS_VIRTIO_VIRTIO_NET_H

#include <stdint.h>
#include "virtio.h"
#include "../../net/netdev.h"

/* Virtio-net device config structure */
typedef struct virtio_net_config {
    uint8_t mac[6];              /* MAC address */
    uint16_t status;             /* Device status */
    uint16_t max_virtqueue_pairs; /* Maximum number of virtqueue pairs */
} __attribute__((packed)) virtio_net_config_t;

/* Virtio-net packet header (prepended to each packet) */
typedef struct virtio_net_hdr {
    uint8_t flags;               /* Flags */
    uint8_t gso_type;            /* GSO type */
    uint16_t hdr_len;            /* Header length */
    uint16_t gso_size;           /* GSO size */
    uint16_t csum_start;         /* Checksum start */
    uint16_t csum_offset;        /* Checksum offset */
} __attribute__((packed)) virtio_net_hdr_t;

/* Virtio-net feature bits */
#define VIRTIO_NET_F_CSUM       (1 << 0)   /* Host handles partial checksums */
#define VIRTIO_NET_F_GUEST_CSUM (1 << 1)   /* Guest handles partial checksums */
#define VIRTIO_NET_F_MAC        (1 << 5)   /* Device has given MAC address */
#define VIRTIO_NET_F_GSO        (1 << 6)   /* Guest can handle GSO */
#define VIRTIO_NET_F_GUEST_TSO4 (1 << 7)   /* Guest can receive TSOv4 */
#define VIRTIO_NET_F_GUEST_TSO6 (1 << 8)   /* Guest can receive TSOv6 */
#define VIRTIO_NET_F_STATUS     (1 << 16)  /* Config status field available */
#define VIRTIO_NET_F_MRG_RXBUF  (1 << 15)  /* Guest can merge RX buffers */

/* Virtio-net device structure */
typedef struct virtio_net_dev {
    virtio_device_t *vdev;       /* Virtio device */
    netdev_t netdev;             /* Network device interface */

    /* Virtqueues */
    virtqueue_t *rx_vq;          /* Receive queue */
    virtqueue_t *tx_vq;          /* Transmit queue */

    /* MAC address */
    uint8_t mac[6];

    /* RX buffer management */
    #define VIRTIO_NET_RX_BUFS 64
    void *rx_bufs[VIRTIO_NET_RX_BUFS];

    /* Statistics */
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;

    spinlock_t tx_lock;
} virtio_net_dev_t;

/* Probe function */
void virtio_net_probe(virtio_device_t *vdev);

#endif /* _DRIVERS_VIRTIO_VIRTIO_NET_H */
