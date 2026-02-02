/* ip.c - Internet Protocol Implementation */

#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "../debug/debug.h"
#include "../lib/string.h"

/* Next IP packet ID */
static uint16_t next_ip_id = 1;

/*
 * Calculate IP checksum
 */
uint16_t ip_checksum(const void *data, uint32_t len) {
    const uint16_t *buf = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }

    /* Add odd byte if present */
    if (len > 0) {
        sum += *(const uint8_t *)buf;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)~sum;
}

/*
 * Convert host byte order to network byte order (16-bit)
 */
static inline uint16_t htons(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

/*
 * Convert network byte order to host byte order (16-bit)
 */
static inline uint16_t ntohs(uint16_t val) {
    return htons(val);  /* Same operation */
}

/*
 * Receive and process an IP packet
 */
int ip_receive(packet_t *pkt) {
    if (!pkt || !pkt->data || pkt->len < sizeof(ip_header_t)) {
        return -22;  /* EINVAL */
    }

    ip_header_t *ip = (ip_header_t *)pkt->data;

    /* Verify version */
    if ((ip->version_ihl >> 4) != 4) {
        DEBUG("IP: Invalid version %d", ip->version_ihl >> 4);
        return -22;
    }

    /* Get header length */
    uint32_t ihl = (ip->version_ihl & 0x0F) * 4;
    if (ihl < 20 || ihl > pkt->len) {
        DEBUG("IP: Invalid header length %u", ihl);
        return -22;
    }

    /* Verify checksum */
    uint16_t saved_checksum = ip->checksum;
    ip->checksum = 0;
    uint16_t calculated = ip_checksum(ip, ihl);
    ip->checksum = saved_checksum;

    if (calculated != saved_checksum) {
        DEBUG("IP: Checksum mismatch (got 0x%x, expected 0x%x)",
              ntohs(saved_checksum), ntohs(calculated));
        return -22;
    }

    /* Extract payload */
    uint8_t *payload = pkt->data + ihl;
    uint32_t payload_len = ntohs(ip->total_len) - ihl;

    DEBUG("IP: Received packet proto=%d src=%d.%d.%d.%d dst=%d.%d.%d.%d len=%u",
          ip->protocol,
          (ip->src_ip >> 24) & 0xFF, (ip->src_ip >> 16) & 0xFF,
          (ip->src_ip >> 8) & 0xFF, ip->src_ip & 0xFF,
          (ip->dst_ip >> 24) & 0xFF, (ip->dst_ip >> 16) & 0xFF,
          (ip->dst_ip >> 8) & 0xFF, ip->dst_ip & 0xFF,
          payload_len);

    /* Dispatch by protocol */
    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            return icmp_receive(ip->src_ip, ip->dst_ip, payload, payload_len);

        case IP_PROTO_UDP:
            return udp_receive(ip->src_ip, ip->dst_ip, payload, payload_len);

        case IP_PROTO_TCP:
            DEBUG("IP: TCP not implemented yet");
            return -95;  /* EOPNOTSUPP */

        default:
            DEBUG("IP: Unknown protocol %d", ip->protocol);
            return -95;
    }
}

/*
 * Send an IP packet
 */
int ip_send(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
            const void *data, uint32_t len) {
    /* Find appropriate network device */
    netdev_t *dev = netdev_get_by_ip(src_ip);
    if (!dev) {
        DEBUG("IP: No route to %d.%d.%d.%d",
              (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
              (dst_ip >> 8) & 0xFF, dst_ip & 0xFF);
        return -101;  /* ENETUNREACH */
    }

    /* Allocate packet */
    packet_t *pkt = packet_alloc(sizeof(ip_header_t) + len);
    if (!pkt) {
        return -12;  /* ENOMEM */
    }

    /* Build IP header */
    ip_header_t *ip = (ip_header_t *)pkt->data;
    memset(ip, 0, sizeof(ip_header_t));

    ip->version_ihl = 0x45;  /* IPv4, 20-byte header */
    ip->tos = 0;
    ip->total_len = htons(sizeof(ip_header_t) + len);
    ip->id = htons(next_ip_id++);
    ip->frag_offset = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->src_ip = src_ip;
    ip->dst_ip = dst_ip;

    /* Calculate checksum */
    ip->checksum = 0;
    ip->checksum = ip_checksum(ip, sizeof(ip_header_t));

    /* Copy payload */
    memcpy(pkt->data + sizeof(ip_header_t), data, len);
    pkt->len = sizeof(ip_header_t) + len;

    /* Transmit */
    int ret = dev->ops->transmit(dev, pkt);
    packet_free(pkt);

    return ret;
}
