/* icmp.c - ICMP Implementation */

#include "icmp.h"
#include "ip.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "../mm/slab.h"

/*
 * Convert host to network byte order (16-bit)
 */
static inline uint16_t htons(uint16_t val) {
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF);
}

static inline uint16_t ntohs(uint16_t val) {
    return htons(val);
}

/*
 * Receive ICMP packet
 */
int icmp_receive(uint32_t src_ip, uint32_t dst_ip, const void *data, uint32_t len) {
    if (!data || len < sizeof(icmp_header_t)) {
        return -22;  /* EINVAL */
    }

    const icmp_header_t *icmp = (const icmp_header_t *)data;

    DEBUG("ICMP: Received type=%d code=%d id=%u seq=%u",
          icmp->type, icmp->code, ntohs(icmp->id), ntohs(icmp->sequence));

    /* Handle echo request (ping) */
    if (icmp->type == ICMP_ECHO_REQUEST) {
        /* Extract payload */
        const void *payload = (const uint8_t *)data + sizeof(icmp_header_t);
        uint32_t payload_len = len - sizeof(icmp_header_t);

        /* Send echo reply */
        return icmp_send_echo_reply(src_ip, icmp->id, icmp->sequence,
                                    payload, payload_len);
    }

    return 0;
}

/*
 * Send ICMP echo reply
 */
int icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq,
                         const void *data, uint32_t len) {
    /* Allocate buffer for ICMP packet */
    uint32_t total_len = sizeof(icmp_header_t) + len;
    uint8_t *buf = kmalloc(total_len);
    if (!buf) {
        return -12;  /* ENOMEM */
    }

    /* Build ICMP header */
    icmp_header_t *icmp = (icmp_header_t *)buf;
    icmp->type = ICMP_ECHO_REPLY;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = id;
    icmp->sequence = seq;

    /* Copy payload */
    if (data && len > 0) {
        memcpy(buf + sizeof(icmp_header_t), data, len);
    }

    /* Calculate checksum */
    icmp->checksum = ip_checksum(buf, total_len);

    DEBUG("ICMP: Sending echo reply to %d.%d.%d.%d id=%u seq=%u",
          (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
          (dst_ip >> 8) & 0xFF, dst_ip & 0xFF,
          ntohs(id), ntohs(seq));

    /* Send via IP */
    int ret = ip_send(IP_LOCALHOST, dst_ip, IP_PROTO_ICMP, buf, total_len);

    kfree(buf);
    return ret;
}
