/* socket.c - Socket Implementation */

#include "socket.h"
#include "udp.h"
#include "ip.h"
#include "../debug/debug.h"
#include "../mm/slab.h"
#include "../lib/string.h"

/* Socket table */
#define MAX_SOCKETS     256
static socket_t *sockets[MAX_SOCKETS];
static spinlock_t socket_table_lock = SPINLOCK_INIT;

/* Port allocation */
static uint16_t next_ephemeral_port = 32768;

/*
 * Initialize socket subsystem
 */
void socket_init(void) {
    memset(sockets, 0, sizeof(sockets));
    spin_init(&socket_table_lock);
}

/*
 * Create a socket
 */
socket_t *socket_create(int type) {
    if (type != SOCK_DGRAM) {
        DEBUG("socket_create: Only SOCK_DGRAM supported for now");
        return NULL;  /* Only UDP for now */
    }

    socket_t *sock = kmalloc(sizeof(socket_t));
    if (!sock) return NULL;

    memset(sock, 0, sizeof(socket_t));
    sock->type = type;
    sock->local_ip = 0;
    sock->local_port = 0;
    sock->remote_ip = 0;
    sock->remote_port = 0;
    sock->bound = false;
    sock->connected = false;
    spin_init(&sock->lock);

    /* Allocate receive buffer */
    sock->recv_buf = kmalloc(sizeof(socket_recv_buf_t));
    if (!sock->recv_buf) {
        kfree(sock);
        return NULL;
    }
    memset(sock->recv_buf, 0, sizeof(socket_recv_buf_t));

    /* Add to socket table */
    uint64_t flags = spin_lock_irqsave(&socket_table_lock);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i]) {
            sockets[i] = sock;
            spin_unlock_irqrestore(&socket_table_lock, flags);
            return sock;
        }
    }
    spin_unlock_irqrestore(&socket_table_lock, flags);

    /* No free slots */
    kfree(sock->recv_buf);
    kfree(sock);
    return NULL;
}

/*
 * Destroy a socket
 */
void socket_destroy(socket_t *sock) {
    if (!sock) return;

    /* Remove from socket table */
    uint64_t flags = spin_lock_irqsave(&socket_table_lock);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i] == sock) {
            sockets[i] = NULL;
            break;
        }
    }
    spin_unlock_irqrestore(&socket_table_lock, flags);

    if (sock->recv_buf) kfree(sock->recv_buf);
    kfree(sock);
}

/*
 * Bind socket to address
 */
int socket_bind(socket_t *sock, uint32_t ip, uint16_t port) {
    if (!sock) return -22;  /* EINVAL */

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (sock->bound) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -22;  /* Already bound */
    }

    sock->local_ip = ip ? ip : IP_LOCALHOST;
    sock->local_port = port;
    sock->bound = true;

    spin_unlock_irqrestore(&sock->lock, flags);

    DEBUG("Socket bound to %d.%d.%d.%d:%u",
          (sock->local_ip >> 24) & 0xFF, (sock->local_ip >> 16) & 0xFF,
          (sock->local_ip >> 8) & 0xFF, sock->local_ip & 0xFF,
          sock->local_port);

    return 0;
}

/*
 * Connect socket to remote address
 */
int socket_connect(socket_t *sock, uint32_t ip, uint16_t port) {
    if (!sock) return -22;

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    sock->remote_ip = ip;
    sock->remote_port = port;
    sock->connected = true;

    /* Auto-bind if not already bound */
    if (!sock->bound) {
        sock->local_ip = IP_LOCALHOST;
        sock->local_port = next_ephemeral_port++;
        sock->bound = true;
    }

    spin_unlock_irqrestore(&sock->lock, flags);
    return 0;
}

/*
 * Send data via socket
 */
int socket_sendto(socket_t *sock, const void *buf, size_t len,
                  uint32_t dst_ip, uint16_t dst_port) {
    if (!sock || !buf) return -22;

    /* Auto-bind if not already bound */
    if (!sock->bound) {
        socket_bind(sock, IP_LOCALHOST, next_ephemeral_port++);
    }

    /* Use connected address if not specified */
    if (dst_ip == 0 && sock->connected) {
        dst_ip = sock->remote_ip;
        dst_port = sock->remote_port;
    }

    if (dst_ip == 0) return -22;

    /* Send via UDP */
    return udp_send(sock->local_ip, sock->local_port, dst_ip, dst_port, buf, len);
}

/*
 * Receive data from socket
 */
ssize_t socket_recvfrom(socket_t *sock, void *buf, size_t len,
                        uint32_t *src_ip, uint16_t *src_port) {
    if (!sock || !buf) return -22;

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (sock->recv_buf->len == 0) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -11;  /* EAGAIN - no data */
    }

    /* Copy data */
    size_t copy_len = (len < sock->recv_buf->len) ? len : sock->recv_buf->len;
    memcpy(buf, sock->recv_buf->data, copy_len);

    if (src_ip) *src_ip = sock->recv_buf->src_ip;
    if (src_port) *src_port = sock->recv_buf->src_port;

    /* Clear buffer */
    sock->recv_buf->len = 0;

    spin_unlock_irqrestore(&sock->lock, flags);
    return (ssize_t)copy_len;
}

/*
 * Deliver UDP packet to socket
 */
int socket_udp_deliver(uint16_t port, uint32_t src_ip, uint16_t src_port,
                       const void *data, uint32_t len) {
    /* Find socket bound to this port */
    uint64_t flags = spin_lock_irqsave(&socket_table_lock);

    for (int i = 0; i < MAX_SOCKETS; i++) {
        socket_t *sock = sockets[i];
        if (sock && sock->bound && sock->local_port == port) {
            uint64_t sflags = spin_lock_irqsave(&sock->lock);

            /* Store in receive buffer (overwrite if full) */
            uint32_t copy_len = (len < SOCKET_RECV_BUF_SIZE) ? len : SOCKET_RECV_BUF_SIZE;
            memcpy(sock->recv_buf->data, data, copy_len);
            sock->recv_buf->len = copy_len;
            sock->recv_buf->src_ip = src_ip;
            sock->recv_buf->src_port = src_port;

            spin_unlock_irqrestore(&sock->lock, sflags);
            spin_unlock_irqrestore(&socket_table_lock, flags);

            DEBUG("UDP packet delivered to socket on port %u", port);
            return 0;
        }
    }

    spin_unlock_irqrestore(&socket_table_lock, flags);
    DEBUG("UDP: No socket listening on port %u", port);
    return -111;  /* ECONNREFUSED */
}
