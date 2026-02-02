/* tcp.h - TCP Protocol Implementation */
#ifndef _NET_TCP_H
#define _NET_TCP_H

#include <stdint.h>
#include "../sync/spinlock.h"

/* TCP Header (20 bytes minimum) */
typedef struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;  /* Upper 4 bits = header length in 32-bit words */
    uint8_t  flags;        /* Control flags */
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

/* TCP Flags */
#define TCP_FIN  (1 << 0)
#define TCP_SYN  (1 << 1)
#define TCP_RST  (1 << 2)
#define TCP_PSH  (1 << 3)
#define TCP_ACK  (1 << 4)
#define TCP_URG  (1 << 5)

/* TCP States */
typedef enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

/* TCP Connection Block */
typedef struct tcp_connection {
    tcp_state_t state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;

    /* Sequence numbers */
    uint32_t snd_una;      /* Send unacknowledged */
    uint32_t snd_nxt;      /* Send next */
    uint32_t rcv_nxt;      /* Receive next */
    uint16_t window_size;  /* Our receive window */

    /* Buffers */
    uint8_t *send_buf;
    uint32_t send_buf_size;
    uint32_t send_buf_len;
    uint8_t *recv_buf;
    uint32_t recv_buf_size;
    uint32_t recv_buf_len;

    /* Timer for retransmission */
    uint64_t retrans_timer;
    uint32_t retrans_count;

    /* Backlog for listening sockets */
    struct tcp_connection **pending_conns;
    uint32_t pending_count;
    uint32_t backlog;

    spinlock_t lock;
} tcp_connection_t;

/* Functions */
void tcp_init(void);
int tcp_receive(uint32_t src_ip, uint32_t dst_ip, const void *data, uint32_t len);
int tcp_send(tcp_connection_t *conn, const void *data, uint32_t len);
int tcp_connect(tcp_connection_t *conn, uint32_t dst_ip, uint16_t dst_port);
int tcp_listen(tcp_connection_t *conn, uint16_t port, int backlog);
tcp_connection_t *tcp_accept(tcp_connection_t *listener);
int tcp_close(tcp_connection_t *conn);

/* Create/destroy connection structures */
tcp_connection_t *tcp_connection_create(void);
void tcp_connection_destroy(tcp_connection_t *conn);

#endif /* _NET_TCP_H */
