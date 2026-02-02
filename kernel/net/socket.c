/* socket.c - Socket Implementation */

#include "socket.h"
#include "udp.h"
#include "tcp.h"
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
socket_t *socket_create(int domain, int type) {
    if (type != SOCK_DGRAM && type != SOCK_STREAM) {
        DEBUG("socket_create: Invalid socket type %d", type);
        return NULL;
    }

    if (domain != AF_INET && domain != AF_UNIX) {
        DEBUG("socket_create: Invalid domain %d", domain);
        return NULL;
    }

    socket_t *sock = kmalloc(sizeof(socket_t));
    if (!sock) return NULL;

    memset(sock, 0, sizeof(socket_t));
    sock->domain = domain;
    sock->type = type;
    sock->local_ip = 0;
    sock->local_port = 0;
    sock->remote_ip = 0;
    sock->remote_port = 0;
    sock->bound = false;
    sock->connected = false;
    sock->listening = false;
    spin_init(&sock->lock);

    if (type == SOCK_STREAM) {
        /* Create TCP connection */
        sock->tcp_conn = tcp_connection_create();
        if (!sock->tcp_conn) {
            kfree(sock);
            return NULL;
        }
    } else {
        /* Allocate receive buffer for UDP */
        sock->recv_buf = kmalloc(sizeof(socket_recv_buf_t));
        if (!sock->recv_buf) {
            kfree(sock);
            return NULL;
        }
        memset(sock->recv_buf, 0, sizeof(socket_recv_buf_t));
    }

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

    /* Clean up TCP connection */
    if (sock->tcp_conn) {
        tcp_close(sock->tcp_conn);
        tcp_connection_destroy(sock->tcp_conn);
    }

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

    /* Auto-bind if not already bound */
    if (!sock->bound) {
        sock->local_ip = IP_LOCALHOST;
        sock->local_port = next_ephemeral_port++;
        sock->bound = true;
    }

    sock->remote_ip = ip;
    sock->remote_port = port;
    sock->connected = true;

    /* For TCP, initiate connection */
    if (sock->type == SOCK_STREAM && sock->tcp_conn) {
        sock->tcp_conn->local_ip = sock->local_ip;
        sock->tcp_conn->local_port = sock->local_port;
        int ret = tcp_connect(sock->tcp_conn, ip, port);
        spin_unlock_irqrestore(&sock->lock, flags);
        return ret;
    }

    spin_unlock_irqrestore(&sock->lock, flags);
    return 0;
}

/*
 * Listen for connections (SOCK_STREAM only)
 */
int socket_listen(socket_t *sock, int backlog) {
    if (!sock || sock->type != SOCK_STREAM) return -22;

    if (!sock->bound) {
        DEBUG("socket_listen: Socket not bound");
        return -22;
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (!sock->tcp_conn) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -22;
    }

    sock->tcp_conn->local_ip = sock->local_ip;
    sock->tcp_conn->local_port = sock->local_port;
    int ret = tcp_listen(sock->tcp_conn, sock->local_port, backlog);
    if (ret == 0) {
        sock->listening = true;
    }

    spin_unlock_irqrestore(&sock->lock, flags);
    return ret;
}

/*
 * Accept incoming connection (SOCK_STREAM only)
 */
socket_t *socket_accept(socket_t *sock) {
    if (!sock || sock->type != SOCK_STREAM || !sock->listening) {
        return NULL;
    }

    /* Try to accept a connection */
    tcp_connection_t *new_conn = tcp_accept(sock->tcp_conn);
    if (!new_conn) {
        return NULL;  /* No connections ready */
    }

    /* Create new socket for accepted connection */
    socket_t *new_sock = kmalloc(sizeof(socket_t));
    if (!new_sock) {
        tcp_connection_destroy(new_conn);
        return NULL;
    }

    memset(new_sock, 0, sizeof(socket_t));
    new_sock->type = SOCK_STREAM;
    new_sock->local_ip = new_conn->local_ip;
    new_sock->local_port = new_conn->local_port;
    new_sock->remote_ip = new_conn->remote_ip;
    new_sock->remote_port = new_conn->remote_port;
    new_sock->bound = true;
    new_sock->connected = true;
    new_sock->listening = false;
    new_sock->tcp_conn = new_conn;
    spin_init(&new_sock->lock);

    /* Add to socket table */
    uint64_t flags = spin_lock_irqsave(&socket_table_lock);
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i]) {
            sockets[i] = new_sock;
            spin_unlock_irqrestore(&socket_table_lock, flags);
            return new_sock;
        }
    }
    spin_unlock_irqrestore(&socket_table_lock, flags);

    /* No free slots */
    tcp_connection_destroy(new_conn);
    kfree(new_sock);
    return NULL;
}

/*
 * Send data via socket
 */
int socket_sendto(socket_t *sock, const void *buf, size_t len,
                  uint32_t dst_ip, uint16_t dst_port) {
    if (!sock || !buf) return -22;

    /* For TCP, use tcp_send */
    if (sock->type == SOCK_STREAM) {
        if (!sock->connected || !sock->tcp_conn) {
            return -107;  /* ENOTCONN */
        }
        return tcp_send(sock->tcp_conn, buf, len);
    }

    /* UDP path */
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

    /* For TCP, read from TCP receive buffer */
    if (sock->type == SOCK_STREAM) {
        if (!sock->connected || !sock->tcp_conn) {
            return -107;  /* ENOTCONN */
        }

        uint64_t flags = spin_lock_irqsave(&sock->tcp_conn->lock);

        if (sock->tcp_conn->recv_buf_len == 0) {
            spin_unlock_irqrestore(&sock->tcp_conn->lock, flags);
            return -11;  /* EAGAIN - no data */
        }

        /* Copy data from TCP buffer */
        size_t copy_len = (len < sock->tcp_conn->recv_buf_len) ?
                          len : sock->tcp_conn->recv_buf_len;
        memcpy(buf, sock->tcp_conn->recv_buf, copy_len);

        /* Remove copied data (shift remaining) */
        if (copy_len < sock->tcp_conn->recv_buf_len) {
            memmove(sock->tcp_conn->recv_buf,
                   sock->tcp_conn->recv_buf + copy_len,
                   sock->tcp_conn->recv_buf_len - copy_len);
        }
        sock->tcp_conn->recv_buf_len -= copy_len;

        if (src_ip) *src_ip = sock->remote_ip;
        if (src_port) *src_port = sock->remote_port;

        spin_unlock_irqrestore(&sock->tcp_conn->lock, flags);
        return (ssize_t)copy_len;
    }

    /* UDP path */
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
