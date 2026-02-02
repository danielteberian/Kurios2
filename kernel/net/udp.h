/* udp.h - User Datagram Protocol */
#ifndef _NET_UDP_H
#define _NET_UDP_H

#include <stdint.h>

/* UDP header */
typedef struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

/* UDP functions */
int udp_receive(uint32_t src_ip, uint32_t dst_ip, const void *data, uint32_t len);
int udp_send(uint32_t src_ip, uint16_t src_port,
             uint32_t dst_ip, uint16_t dst_port,
             const void *data, uint32_t len);

#endif /* _NET_UDP_H */
