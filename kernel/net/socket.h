/* socket.h - Socket Layer */
#ifndef _NET_SOCKET_H
#define _NET_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../sync/spinlock.h"
#include "../include/types.h"  /* For ssize_t */

/* Socket address family */
#define AF_INET         2

/* Socket types */
#define SOCK_STREAM     1   /* TCP */
#define SOCK_DGRAM      2   /* UDP */

/* Socket address structure (IPv4) */
typedef struct sockaddr_in {
    uint16_t sin_family;        /* AF_INET */
    uint16_t sin_port;          /* Port number (network byte order) */
    uint32_t sin_addr;          /* IP address (network byte order) */
    uint8_t  sin_zero[8];       /* Padding */
} sockaddr_in_t;

/* Generic socket address */
typedef struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
} sockaddr_t;

/* Socket receive buffer */
#define SOCKET_RECV_BUF_SIZE    8192

typedef struct socket_recv_buf {
    uint8_t data[SOCKET_RECV_BUF_SIZE];
    uint32_t len;
    uint32_t src_ip;
    uint16_t src_port;
} socket_recv_buf_t;

/* Socket structure */
typedef struct socket {
    int type;                   /* SOCK_STREAM or SOCK_DGRAM */
    uint32_t local_ip;          /* Local IP address */
    uint16_t local_port;        /* Local port */
    uint32_t remote_ip;         /* Remote IP (for connected sockets) */
    uint16_t remote_port;       /* Remote port */
    bool bound;                 /* Bound to an address */
    bool connected;             /* Connected to remote */

    /* Receive buffer (simple circular buffer for now) */
    socket_recv_buf_t *recv_buf;
    spinlock_t lock;
} socket_t;

/* Socket initialization */
void socket_init(void);

/* Socket operations */
socket_t *socket_create(int type);
void socket_destroy(socket_t *sock);
int socket_bind(socket_t *sock, uint32_t ip, uint16_t port);
int socket_connect(socket_t *sock, uint32_t ip, uint16_t port);
int socket_sendto(socket_t *sock, const void *buf, size_t len,
                  uint32_t dst_ip, uint16_t dst_port);
ssize_t socket_recvfrom(socket_t *sock, void *buf, size_t len,
                        uint32_t *src_ip, uint16_t *src_port);

/* Internal delivery functions */
int socket_udp_deliver(uint16_t port, uint32_t src_ip, uint16_t src_port,
                       const void *data, uint32_t len);

#endif /* _NET_SOCKET_H */
