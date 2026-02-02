/* loopback.c - Loopback Network Device (127.0.0.1) */

#include "netdev.h"
#include "ip.h"
#include "../debug/debug.h"
#include "../lib/string.h"

/* Loopback device instance */
static netdev_t loopback_dev;

/*
 * Loopback device operations
 */
static int loopback_open(netdev_t *dev) {
    (void)dev;
    INFO("Loopback device opened");
    return 0;
}

static int loopback_close(netdev_t *dev) {
    (void)dev;
    return 0;
}

static int loopback_transmit(netdev_t *dev, packet_t *pkt) {
    if (!dev || !pkt) return -22;

    /* For loopback, receiving our own transmission is instant */
    DEBUG("Loopback: transmitting %u bytes", pkt->len);

    /* Make a copy and receive it */
    packet_t *rx_pkt = packet_alloc(pkt->len);
    if (!rx_pkt) return -12;

    memcpy(rx_pkt->data, pkt->data, pkt->len);
    rx_pkt->len = pkt->len;

    /* Receive the packet */
    netdev_receive(dev, rx_pkt);

    packet_free(rx_pkt);
    return 0;
}

static netdev_ops_t loopback_ops = {
    .open = loopback_open,
    .close = loopback_close,
    .transmit = loopback_transmit,
};

/*
 * Initialize loopback device
 */
void loopback_init(void) {
    memset(&loopback_dev, 0, sizeof(netdev_t));

    strcpy(loopback_dev.name, "lo");
    memset(loopback_dev.mac, 0, ETH_ALEN);
    loopback_dev.ip = IP_LOCALHOST;  /* 127.0.0.1 */
    loopback_dev.netmask = IP_ADDR(255, 0, 0, 0);
    loopback_dev.flags = NETDEV_UP | NETDEV_RUNNING | NETDEV_LOOPBACK;
    loopback_dev.mtu = 65536;
    loopback_dev.ops = &loopback_ops;
    loopback_dev.private = NULL;
    spin_init(&loopback_dev.lock);

    if (netdev_register(&loopback_dev) < 0) {
        ERROR("Failed to register loopback device");
        return;
    }

    INFO("Loopback device initialized: 127.0.0.1");
}
