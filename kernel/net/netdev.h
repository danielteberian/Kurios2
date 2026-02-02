/* netdev.h - Network Device Abstraction Layer */
#ifndef _NET_NETDEV_H
#define _NET_NETDEV_H

#include <stdint.h>
#include <stdbool.h>
#include "../sync/spinlock.h"

/* Maximum number of network devices */
#define MAX_NETDEVS     16

/* Network device flags */
#define NETDEV_UP       0x01    /* Device is up */
#define NETDEV_RUNNING  0x02    /* Device is running */
#define NETDEV_LOOPBACK 0x04    /* Loopback device */

/* Ethernet addresses are 6 bytes */
#define ETH_ALEN        6

/* Maximum transmission unit */
#define ETH_MTU         1500
#define ETH_FRAME_LEN   1514    /* Header (14) + payload (1500) */

/* Packet buffer */
typedef struct packet {
    uint8_t *data;              /* Packet data */
    uint32_t len;               /* Data length */
    uint32_t capacity;          /* Buffer capacity */
    uint32_t protocol;          /* Protocol type */
    struct netdev *dev;         /* Device this packet is for */
    void *private;              /* Protocol-specific data */
} packet_t;

/* Forward declaration */
typedef struct netdev netdev_t;

/* Network device operations */
typedef struct netdev_ops {
    int (*open)(netdev_t *dev);
    int (*close)(netdev_t *dev);
    int (*transmit)(netdev_t *dev, packet_t *pkt);
} netdev_ops_t;

/* Network device structure */
struct netdev {
    char name[16];              /* Device name (e.g., "lo", "eth0") */
    uint8_t mac[ETH_ALEN];      /* MAC address */
    uint32_t ip;                /* IP address (network byte order) */
    uint32_t netmask;           /* Netmask */
    uint32_t flags;             /* Device flags */
    uint32_t mtu;               /* Maximum transmission unit */

    netdev_ops_t *ops;          /* Device operations */
    void *private;              /* Driver private data */

    spinlock_t lock;            /* Device lock */
};

/* Network subsystem initialization */
void net_init(void);

/* Network device management */
int netdev_register(netdev_t *dev);
netdev_t *netdev_get_by_name(const char *name);
netdev_t *netdev_get_by_ip(uint32_t ip);

/* Packet handling */
packet_t *packet_alloc(uint32_t size);
void packet_free(packet_t *pkt);
int netdev_receive(netdev_t *dev, packet_t *pkt);

#endif /* _NET_NETDEV_H */
