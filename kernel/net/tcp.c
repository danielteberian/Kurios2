/* tcp.c - TCP Protocol Implementation */

#include "tcp.h"
#include "ip.h"
#include "../mm/slab.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "../drivers/pit.h"

/* Byte order conversion functions */
static inline uint16_t htons(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

static inline uint16_t ntohs(uint16_t x) {
    return htons(x);  /* Same operation */
}

static inline uint32_t ntohl(uint32_t x) {
    return htonl(x);  /* Same operation */
}

/* TCP protocol number */
#define IP_PROTO_TCP 6

/* TCP buffer sizes */
#define TCP_SEND_BUF_SIZE (64 * 1024)  /* 64KB */
#define TCP_RECV_BUF_SIZE (16 * 1024)  /* 16KB window */

/* TCP retransmission timeout (milliseconds) */
#define TCP_RETRANS_TIMEOUT 1000

/* TCP pseudo-header for checksum calculation */
typedef struct tcp_pseudo_header {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_length;
} __attribute__((packed)) tcp_pseudo_header_t;

/*
 * Calculate TCP checksum (includes pseudo-header)
 */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             const tcp_header_t *tcp, uint32_t len) {
    /* Build pseudo-header */
    tcp_pseudo_header_t pseudo;
    pseudo.src_ip = src_ip;
    pseudo.dst_ip = dst_ip;
    pseudo.zero = 0;
    pseudo.protocol = IP_PROTO_TCP;
    pseudo.tcp_length = htons(len);

    /* Calculate checksum over pseudo-header + TCP segment */
    uint32_t sum = 0;

    /* Add pseudo-header (careful with alignment) */
    const uint8_t *p_bytes = (const uint8_t *)&pseudo;
    for (unsigned i = 0; i < sizeof(pseudo); i += 2) {
        sum += (p_bytes[i] << 8) | p_bytes[i + 1];
    }

    /* Add TCP header and data (careful with alignment) */
    const uint8_t *t_bytes = (const uint8_t *)tcp;
    for (unsigned i = 0; i < len - 1; i += 2) {
        sum += (t_bytes[i] << 8) | t_bytes[i + 1];
    }

    /* Handle odd byte */
    if (len & 1) {
        sum += t_bytes[len - 1] << 8;
    }

    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

/*
 * Get initial sequence number (simple: use time-based value)
 */
static uint32_t tcp_get_isn(void) {
    return (uint32_t)pit_get_ticks() * 12345;
}

/*
 * Create a TCP connection structure
 */
tcp_connection_t *tcp_connection_create(void) {
    tcp_connection_t *conn = kmalloc(sizeof(tcp_connection_t));
    if (!conn) {
        return NULL;
    }

    memset(conn, 0, sizeof(tcp_connection_t));
    conn->state = TCP_CLOSED;
    conn->window_size = TCP_RECV_BUF_SIZE;

    /* Allocate send buffer */
    conn->send_buf = kmalloc(TCP_SEND_BUF_SIZE);
    if (!conn->send_buf) {
        kfree(conn);
        return NULL;
    }
    conn->send_buf_size = TCP_SEND_BUF_SIZE;
    conn->send_buf_len = 0;

    /* Allocate receive buffer */
    conn->recv_buf = kmalloc(TCP_RECV_BUF_SIZE);
    if (!conn->recv_buf) {
        kfree(conn->send_buf);
        kfree(conn);
        return NULL;
    }
    conn->recv_buf_size = TCP_RECV_BUF_SIZE;
    conn->recv_buf_len = 0;

    spin_init(&conn->lock);

    return conn;
}

/*
 * Destroy a TCP connection structure
 */
void tcp_connection_destroy(tcp_connection_t *conn) {
    if (!conn) {
        return;
    }

    if (conn->send_buf) {
        kfree(conn->send_buf);
    }
    if (conn->recv_buf) {
        kfree(conn->recv_buf);
    }
    if (conn->pending_conns) {
        kfree(conn->pending_conns);
    }

    kfree(conn);
}

/*
 * Send a TCP packet
 */
static int tcp_send_packet(tcp_connection_t *conn, uint8_t flags,
                          const void *data, uint32_t data_len) {
    /* Build TCP header */
    uint32_t total_len = sizeof(tcp_header_t) + data_len;
    uint8_t *packet = kmalloc(total_len);
    if (!packet) {
        return -1;
    }

    tcp_header_t *tcp = (tcp_header_t *)packet;
    tcp->src_port = htons(conn->local_port);
    tcp->dst_port = htons(conn->remote_port);
    tcp->seq_num = htonl(conn->snd_nxt);
    tcp->ack_num = htonl(conn->rcv_nxt);
    tcp->data_offset = (sizeof(tcp_header_t) / 4) << 4;  /* No options */
    tcp->flags = flags;
    tcp->window_size = htons(conn->window_size);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    /* Copy data */
    if (data && data_len > 0) {
        memcpy(packet + sizeof(tcp_header_t), data, data_len);
    }

    /* Calculate checksum */
    tcp->checksum = tcp_checksum(conn->local_ip, conn->remote_ip, tcp, total_len);

    /* Send via IP */
    int ret = ip_send(conn->local_ip, conn->remote_ip, IP_PROTO_TCP, packet, total_len);

    kfree(packet);

    /* Update sequence number if we sent data or SYN/FIN */
    if (data_len > 0 || (flags & (TCP_SYN | TCP_FIN))) {
        conn->snd_nxt += data_len + ((flags & (TCP_SYN | TCP_FIN)) ? 1 : 0);
    }

    return ret;
}

/*
 * Process incoming SYN packet
 */
static void tcp_process_syn(tcp_connection_t *conn, const tcp_header_t *tcp,
                            uint32_t src_ip, uint16_t src_port) {
    uint64_t flags = spin_lock_irqsave(&conn->lock);

    if (conn->state == TCP_LISTEN) {
        /* Create new connection for this incoming request */
        tcp_connection_t *new_conn = tcp_connection_create();
        if (!new_conn) {
            spin_unlock_irqrestore(&conn->lock, flags);
            return;
        }

        new_conn->local_ip = conn->local_ip;
        new_conn->local_port = conn->local_port;
        new_conn->remote_ip = src_ip;
        new_conn->remote_port = src_port;
        new_conn->rcv_nxt = ntohl(tcp->seq_num) + 1;
        new_conn->snd_nxt = tcp_get_isn();
        new_conn->snd_una = new_conn->snd_nxt;
        new_conn->state = TCP_SYN_RECEIVED;

        /* Send SYN-ACK */
        tcp_send_packet(new_conn, TCP_SYN | TCP_ACK, NULL, 0);

        /* Add to pending queue */
        if (conn->pending_count < conn->backlog) {
            conn->pending_conns[conn->pending_count++] = new_conn;
        } else {
            /* Backlog full, drop */
            tcp_connection_destroy(new_conn);
        }
    } else if (conn->state == TCP_SYN_SENT) {
        /* Simultaneous open */
        conn->rcv_nxt = ntohl(tcp->seq_num) + 1;
        conn->state = TCP_SYN_RECEIVED;
        tcp_send_packet(conn, TCP_SYN | TCP_ACK, NULL, 0);
    }

    spin_unlock_irqrestore(&conn->lock, flags);
}

/*
 * Process incoming ACK packet
 */
static void tcp_process_ack(tcp_connection_t *conn, const tcp_header_t *tcp) {
    uint64_t flags = spin_lock_irqsave(&conn->lock);

    uint32_t ack_num = ntohl(tcp->ack_num);

    if (conn->state == TCP_SYN_SENT) {
        if (ack_num == conn->snd_nxt) {
            conn->state = TCP_ESTABLISHED;
            conn->snd_una = ack_num;
            DEBUG("TCP: Connection established");
        }
    } else if (conn->state == TCP_SYN_RECEIVED) {
        if (ack_num == conn->snd_nxt) {
            conn->state = TCP_ESTABLISHED;
            conn->snd_una = ack_num;
        }
    } else if (conn->state == TCP_ESTABLISHED) {
        /* Update send window */
        if (ack_num > conn->snd_una) {
            conn->snd_una = ack_num;
        }
    } else if (conn->state == TCP_FIN_WAIT_1) {
        if (ack_num == conn->snd_nxt) {
            conn->state = TCP_FIN_WAIT_2;
            conn->snd_una = ack_num;
        }
    } else if (conn->state == TCP_CLOSING) {
        if (ack_num == conn->snd_nxt) {
            conn->state = TCP_TIME_WAIT;
            conn->snd_una = ack_num;
        }
    } else if (conn->state == TCP_LAST_ACK) {
        if (ack_num == conn->snd_nxt) {
            conn->state = TCP_CLOSED;
            conn->snd_una = ack_num;
        }
    }

    spin_unlock_irqrestore(&conn->lock, flags);
}

/*
 * Process incoming FIN packet
 */
static void tcp_process_fin(tcp_connection_t *conn, const tcp_header_t *tcp) {
    uint64_t flags = spin_lock_irqsave(&conn->lock);

    conn->rcv_nxt = ntohl(tcp->seq_num) + 1;

    if (conn->state == TCP_ESTABLISHED) {
        conn->state = TCP_CLOSE_WAIT;
        /* Send ACK for FIN */
        tcp_send_packet(conn, TCP_ACK, NULL, 0);
    } else if (conn->state == TCP_FIN_WAIT_1) {
        conn->state = TCP_CLOSING;
        tcp_send_packet(conn, TCP_ACK, NULL, 0);
    } else if (conn->state == TCP_FIN_WAIT_2) {
        conn->state = TCP_TIME_WAIT;
        tcp_send_packet(conn, TCP_ACK, NULL, 0);
    }

    spin_unlock_irqrestore(&conn->lock, flags);
}

/*
 * Process incoming data
 */
static void tcp_process_data(tcp_connection_t *conn, const tcp_header_t *tcp,
                            const uint8_t *data, uint32_t data_len) {
    uint64_t flags = spin_lock_irqsave(&conn->lock);

    if (conn->state != TCP_ESTABLISHED && conn->state != TCP_FIN_WAIT_1 &&
        conn->state != TCP_FIN_WAIT_2) {
        spin_unlock_irqrestore(&conn->lock, flags);
        return;
    }

    uint32_t seq_num = ntohl(tcp->seq_num);

    /* Check if this is the expected sequence number */
    if (seq_num != conn->rcv_nxt) {
        /* Out of order, ignore for now */
        spin_unlock_irqrestore(&conn->lock, flags);
        return;
    }

    /* Copy data to receive buffer */
    uint32_t space = conn->recv_buf_size - conn->recv_buf_len;
    uint32_t to_copy = (data_len < space) ? data_len : space;

    if (to_copy > 0) {
        memcpy(conn->recv_buf + conn->recv_buf_len, data, to_copy);
        conn->recv_buf_len += to_copy;
        conn->rcv_nxt += to_copy;
    }

    /* Send ACK */
    tcp_send_packet(conn, TCP_ACK, NULL, 0);

    spin_unlock_irqrestore(&conn->lock, flags);
}

/*
 * Receive TCP packet (called from IP layer)
 */
int tcp_receive(uint32_t src_ip, uint32_t dst_ip, const void *data, uint32_t len) {
    if (len < sizeof(tcp_header_t)) {
        return -1;
    }

    const tcp_header_t *tcp = (const tcp_header_t *)data;
    uint16_t src_port = ntohs(tcp->src_port);
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint8_t flags = tcp->flags;

    /* Validate checksum */
    uint16_t expected_checksum = tcp_checksum(src_ip, dst_ip, tcp, len);
    if (expected_checksum != 0) {
        DEBUG("TCP: Invalid checksum");
        return -1;
    }

    /* Extract data */
    uint8_t header_len = (tcp->data_offset >> 4) * 4;
    const uint8_t *payload = (const uint8_t *)data + header_len;
    uint32_t payload_len = len - header_len;

    DEBUG("TCP: Received packet src=%u.%u.%u.%u:%u dst=%u.%u.%u.%u:%u flags=0x%x len=%u",
          (src_ip >> 0) & 0xFF, (src_ip >> 8) & 0xFF, (src_ip >> 16) & 0xFF, (src_ip >> 24) & 0xFF, src_port,
          (dst_ip >> 0) & 0xFF, (dst_ip >> 8) & 0xFF, (dst_ip >> 16) & 0xFF, (dst_ip >> 24) & 0xFF, dst_port,
          flags, payload_len);

    /* NOTE: Connection lookup would happen here in a complete implementation.
     * For now, the socket layer manages connections and the actual packet
     * processing is done through the socket API calls (send/recv).
     * The process functions below are unused for now but will be needed
     * when we implement proper connection state tracking. */

    /* Suppress unused function warnings */
    (void)tcp_process_syn;
    (void)tcp_process_ack;
    (void)tcp_process_fin;
    (void)tcp_process_data;
    (void)payload;
    (void)payload_len;

    return 0;
}

/*
 * Send data over TCP connection
 */
int tcp_send(tcp_connection_t *conn, const void *data, uint32_t len) {
    if (!conn || conn->state != TCP_ESTABLISHED) {
        return -1;
    }

    uint64_t flags = spin_lock_irqsave(&conn->lock);

    /* For now, send all data in one packet (no segmentation) */
    int ret = tcp_send_packet(conn, TCP_ACK | TCP_PSH, data, len);

    spin_unlock_irqrestore(&conn->lock, flags);

    return ret;
}

/*
 * Connect to remote host (active open)
 */
int tcp_connect(tcp_connection_t *conn, uint32_t dst_ip, uint16_t dst_port) {
    if (!conn || conn->state != TCP_CLOSED) {
        return -1;
    }

    uint64_t flags = spin_lock_irqsave(&conn->lock);

    conn->remote_ip = dst_ip;
    conn->remote_port = dst_port;
    conn->snd_nxt = tcp_get_isn();
    conn->snd_una = conn->snd_nxt;
    conn->state = TCP_SYN_SENT;

    /* Send SYN */
    int ret = tcp_send_packet(conn, TCP_SYN, NULL, 0);

    spin_unlock_irqrestore(&conn->lock, flags);

    return ret;
}

/*
 * Listen for connections (passive open)
 */
int tcp_listen(tcp_connection_t *conn, uint16_t port, int backlog) {
    if (!conn || conn->state != TCP_CLOSED) {
        return -1;
    }

    uint64_t flags = spin_lock_irqsave(&conn->lock);

    conn->local_port = port;
    conn->backlog = backlog;
    conn->pending_count = 0;

    /* Allocate pending connection queue */
    conn->pending_conns = kmalloc(sizeof(tcp_connection_t *) * backlog);
    if (!conn->pending_conns) {
        spin_unlock_irqrestore(&conn->lock, flags);
        return -1;
    }

    conn->state = TCP_LISTEN;

    spin_unlock_irqrestore(&conn->lock, flags);

    return 0;
}

/*
 * Accept incoming connection
 */
tcp_connection_t *tcp_accept(tcp_connection_t *listener) {
    if (!listener || listener->state != TCP_LISTEN) {
        return NULL;
    }

    uint64_t flags = spin_lock_irqsave(&listener->lock);

    tcp_connection_t *conn = NULL;

    /* Check for completed connections in pending queue */
    for (uint32_t i = 0; i < listener->pending_count; i++) {
        if (listener->pending_conns[i]->state == TCP_ESTABLISHED) {
            conn = listener->pending_conns[i];

            /* Remove from queue */
            for (uint32_t j = i; j < listener->pending_count - 1; j++) {
                listener->pending_conns[j] = listener->pending_conns[j + 1];
            }
            listener->pending_count--;

            break;
        }
    }

    spin_unlock_irqrestore(&listener->lock, flags);

    return conn;
}

/*
 * Close TCP connection
 */
int tcp_close(tcp_connection_t *conn) {
    if (!conn) {
        return -1;
    }

    uint64_t flags = spin_lock_irqsave(&conn->lock);

    if (conn->state == TCP_ESTABLISHED) {
        /* Send FIN */
        tcp_send_packet(conn, TCP_FIN | TCP_ACK, NULL, 0);
        conn->state = TCP_FIN_WAIT_1;
    } else if (conn->state == TCP_CLOSE_WAIT) {
        /* Send FIN */
        tcp_send_packet(conn, TCP_FIN | TCP_ACK, NULL, 0);
        conn->state = TCP_LAST_ACK;
    } else {
        conn->state = TCP_CLOSED;
    }

    spin_unlock_irqrestore(&conn->lock, flags);

    return 0;
}

/*
 * Initialize TCP subsystem
 */
void tcp_init(void) {
    INFO("Initializing TCP...");
    /* Nothing to initialize yet */
    INFO("TCP initialized");
}
