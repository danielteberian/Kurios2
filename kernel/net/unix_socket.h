/* unix_socket.h - Unix Domain Socket Implementation */
#ifndef _NET_UNIX_SOCKET_H
#define _NET_UNIX_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../include/types.h"
#include "../sync/spinlock.h"

/* Unix socket address */
#define AF_UNIX         1
#define UNIX_PATH_MAX   108

typedef struct sockaddr_un {
    uint16_t sun_family;            /* AF_UNIX */
    char sun_path[UNIX_PATH_MAX];   /* Path to socket file */
} sockaddr_un_t;

/* Unix socket buffer size */
#define UNIX_SOCKET_BUF_SIZE    4096

/* Unix socket state */
typedef enum unix_socket_state {
    UNIX_CLOSED,
    UNIX_BOUND,
    UNIX_LISTENING,
    UNIX_CONNECTED
} unix_socket_state_t;

/* Unix socket connection queue */
#define UNIX_CONN_QUEUE_SIZE    5

typedef struct unix_connection_queue {
    struct unix_socket *pending[UNIX_CONN_QUEUE_SIZE];
    int count;
    int backlog;
} unix_connection_queue_t;

/* Unix socket structure */
typedef struct unix_socket {
    int type;                       /* SOCK_STREAM or SOCK_DGRAM */
    unix_socket_state_t state;      /* Socket state */
    char path[UNIX_PATH_MAX];       /* Bound path */

    /* For SOCK_STREAM connections */
    struct unix_socket *peer;       /* Connected peer socket */
    struct unix_socket *listener;   /* Listening socket (for accepted connections) */
    unix_connection_queue_t *conn_queue;  /* Connection queue (for listening sockets) */

    /* Circular buffer for data */
    uint8_t *buffer;                /* Data buffer */
    size_t buf_size;                /* Buffer size */
    size_t buf_head;                /* Write position */
    size_t buf_tail;                /* Read position */
    size_t buf_count;               /* Number of bytes in buffer */

    /* Synchronization */
    spinlock_t lock;
} unix_socket_t;

/* Unix socket operations */
unix_socket_t *unix_socket_create(int type);
void unix_socket_destroy(unix_socket_t *sock);
int unix_socket_bind(unix_socket_t *sock, const char *path);
int unix_socket_connect(unix_socket_t *sock, const char *path);
int unix_socket_listen(unix_socket_t *sock, int backlog);
unix_socket_t *unix_socket_accept(unix_socket_t *sock);
ssize_t unix_socket_send(unix_socket_t *sock, const void *buf, size_t len);
ssize_t unix_socket_recv(unix_socket_t *sock, void *buf, size_t len);

/* Helper functions */
unix_socket_t *unix_socket_find_by_path(const char *path);

#endif /* _NET_UNIX_SOCKET_H */
