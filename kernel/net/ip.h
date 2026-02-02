/* ip.h - Internet Protocol (IPv4) */
#ifndef _NET_IP_H
#define _NET_IP_H

#include <stdint.h>
#include "netdev.h"

/* IP protocol numbers */
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

/* IP header (20 bytes minimum) */
typedef struct ip_header {
    uint8_t  version_ihl;       /* Version (4 bits) + IHL (4 bits) */
    uint8_t  tos;               /* Type of service */
    uint16_t total_len;         /* Total length */
    uint16_t id;                /* Identification */
    uint16_t frag_offset;       /* Flags (3 bits) + Fragment offset (13 bits) */
    uint8_t  ttl;               /* Time to live */
    uint8_t  protocol;          /* Protocol */
    uint16_t checksum;          /* Header checksum */
    uint32_t src_ip;            /* Source IP address */
    uint32_t dst_ip;            /* Destination IP address */
} __attribute__((packed)) ip_header_t;

/* IP address utilities */
#define IP_ADDR(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                             ((uint32_t)(c) << 8) | (uint32_t)(d))

#define IP_LOCALHOST    IP_ADDR(127, 0, 0, 1)

/* IP layer functions */
int ip_receive(packet_t *pkt);
int ip_send(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
            const void *data, uint32_t len);

/* Checksum calculation */
uint16_t ip_checksum(const void *data, uint32_t len);

#endif /* _NET_IP_H */
