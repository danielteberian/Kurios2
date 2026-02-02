/* icmp.h - Internet Control Message Protocol */
#ifndef _NET_ICMP_H
#define _NET_ICMP_H

#include <stdint.h>

/* ICMP message types */
#define ICMP_ECHO_REPLY     0
#define ICMP_DEST_UNREACH   3
#define ICMP_ECHO_REQUEST   8

/* ICMP header */
typedef struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

/* ICMP functions */
int icmp_receive(uint32_t src_ip, uint32_t dst_ip, const void *data, uint32_t len);
int icmp_send_echo_reply(uint32_t dst_ip, uint16_t id, uint16_t seq,
                         const void *data, uint32_t len);

#endif /* _NET_ICMP_H */
