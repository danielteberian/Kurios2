/* udp.c - UDP Implementation */

#include "udp.h"
#include "ip.h"
#include "socket.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "../mm/slab.h"

static inline uint16_t htons(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

static inline uint16_t ntohs(uint16_t val) {
    return htons(val);
}

/*
 * Receive UDP packet
 */
int udp_receive(uint32_t src_ip, uint32_t dst_ip, const void *data, uint32_t len) {
    if (!data || len < sizeof(udp_header_t)) {
        return -22;  /* EINVAL */
    }

    const udp_header_t *udp = (const udp_header_t *)data;
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t dst_port = ntohs(udp->dst_port);

    const void *payload = (const uint8_t *)data + sizeof(udp_header_t);
    uint32_t payload_len = len - sizeof(udp_header_t);

    DEBUG("UDP: Received src=%d.%d.%d.%d:%u dst=%d.%d.%d.%d:%u len=%u",
          (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
          (src_ip >> 8) & 0xFF, src_ip & 0xFF, src_port,
          (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
          (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port,
          payload_len);

    /* Deliver to socket */
    return socket_udp_deliver(dst_port, src_ip, src_port, payload, payload_len);
}

/*
 * Send UDP packet
 */
int udp_send(uint32_t src_ip, uint16_t src_port,
             uint32_t dst_ip, uint16_t dst_port,
             const void *data, uint32_t len) {
    /* Allocate buffer */
    uint32_t total_len = sizeof(udp_header_t) + len;
    uint8_t *buf = kmalloc(total_len);
    if (!buf) {
        return -12;  /* ENOMEM */
    }

    /* Build UDP header */
    udp_header_t *udp = (udp_header_t *)buf;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(total_len);
    udp->checksum = 0;  /* Optional for IPv4 */

    /* Copy payload */
    if (data && len > 0) {
        memcpy(buf + sizeof(udp_header_t), data, len);
    }

    DEBUG("UDP: Sending src=%d.%d.%d.%d:%u dst=%d.%d.%d.%d:%u len=%u",
          (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
          (src_ip >> 8) & 0xFF, src_ip & 0xFF, src_port,
          (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
          (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port,
          len);

    /* Send via IP */
    int ret = ip_send(src_ip, dst_ip, IP_PROTO_UDP, buf, total_len);

    kfree(buf);
    return ret;
}
