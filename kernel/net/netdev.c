/* netdev.c - Network Device Layer */

#include "netdev.h"
#include "ip.h"
#include "../debug/debug.h"
#include "../mm/slab.h"
#include "../lib/string.h"

/* Network device table */
static netdev_t *netdevs[MAX_NETDEVS];
static spinlock_t netdev_lock = SPINLOCK_INIT;
static uint32_t num_netdevs = 0;

/*
 * Initialize network subsystem
 */
void net_init(void) {
    INFO("Initializing network subsystem...");
    memset(netdevs, 0, sizeof(netdevs));
    spin_init(&netdev_lock);
    num_netdevs = 0;
}

/*
 * Register a network device
 */
int netdev_register(netdev_t *dev) {
    if (!dev || !dev->name[0]) {
        return -22;  /* EINVAL */
    }

    uint64_t flags = spin_lock_irqsave(&netdev_lock);

    if (num_netdevs >= MAX_NETDEVS) {
        spin_unlock_irqrestore(&netdev_lock, flags);
        return -28;  /* ENOSPC */
    }

    /* Check for duplicate name */
    for (uint32_t i = 0; i < num_netdevs; i++) {
        if (netdevs[i] && strcmp(netdevs[i]->name, dev->name) == 0) {
            spin_unlock_irqrestore(&netdev_lock, flags);
            return -17;  /* EEXIST */
        }
    }

    netdevs[num_netdevs++] = dev;
    spin_unlock_irqrestore(&netdev_lock, flags);

    INFO("Network device registered: %s (IP: %d.%d.%d.%d)",
         dev->name,
         (dev->ip >> 24) & 0xFF, (dev->ip >> 16) & 0xFF,
         (dev->ip >> 8) & 0xFF, dev->ip & 0xFF);

    return 0;
}

/*
 * Get network device by name
 */
netdev_t *netdev_get_by_name(const char *name) {
    if (!name) return NULL;

    uint64_t flags = spin_lock_irqsave(&netdev_lock);
    for (uint32_t i = 0; i < num_netdevs; i++) {
        if (netdevs[i] && strcmp(netdevs[i]->name, name) == 0) {
            netdev_t *dev = netdevs[i];
            spin_unlock_irqrestore(&netdev_lock, flags);
            return dev;
        }
    }
    spin_unlock_irqrestore(&netdev_lock, flags);
    return NULL;
}

/*
 * Get network device by IP address
 */
netdev_t *netdev_get_by_ip(uint32_t ip) {
    uint64_t flags = spin_lock_irqsave(&netdev_lock);
    for (uint32_t i = 0; i < num_netdevs; i++) {
        if (netdevs[i] && netdevs[i]->ip == ip) {
            netdev_t *dev = netdevs[i];
            spin_unlock_irqrestore(&netdev_lock, flags);
            return dev;
        }
    }
    spin_unlock_irqrestore(&netdev_lock, flags);
    return NULL;
}

/*
 * Allocate a packet buffer
 */
packet_t *packet_alloc(uint32_t size) {
    packet_t *pkt = kmalloc(sizeof(packet_t));
    if (!pkt) return NULL;

    pkt->data = kmalloc(size);
    if (!pkt->data) {
        kfree(pkt);
        return NULL;
    }

    pkt->len = 0;
    pkt->capacity = size;
    pkt->protocol = 0;
    pkt->dev = NULL;
    pkt->private = NULL;

    return pkt;
}

/*
 * Free a packet buffer
 */
void packet_free(packet_t *pkt) {
    if (!pkt) return;
    if (pkt->data) kfree(pkt->data);
    kfree(pkt);
}

/*
 * Receive a packet (called by device drivers)
 */
int netdev_receive(netdev_t *dev, packet_t *pkt) {
    if (!dev || !pkt) return -22;

    pkt->dev = dev;

    /* Pass to IP layer */
    return ip_receive(pkt);
}
