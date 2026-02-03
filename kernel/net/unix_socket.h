/* unix_socket.h - Unix Domain Sockets */
#ifndef _NET_UNIX_SOCKET_H
#define _NET_UNIX_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../sync/spinlock.h"
#include "../include/types.h"

/* Unix socket address structure defined in socket.h */
#define UNIX_PATH_MAX   108

/* Unix socket connection queue entry */
typedef struct unix_conn_queue_entry {
    struct unix_socket *peer;
    struct unix_conn_queue_entry *next;
} unix_conn_queue_entry_t;

/* Unix socket circular buffer for SOCK_STREAM */
#define UNIX_SOCK_BUF_SIZE  8192

typedef struct unix_stream_buf {
    uint8_t data[UNIX_SOCK_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
} unix_stream_buf_t;

/* Unix socket datagram queue entry */
typedef struct unix_dgram_entry {
    uint8_t data[UNIX_SOCK_BUF_SIZE];
    uint32_t len;
    char src_path[UNIX_PATH_MAX];
    struct unix_dgram_entry *next;
} unix_dgram_entry_t;

/* Control message for SCM_RIGHTS (fd passing) */
#define SCM_RIGHTS      1
#define CMSG_MAX_FDS    8

typedef struct unix_cmsg {
    int type;                   /* SCM_RIGHTS */
    int fds[CMSG_MAX_FDS];      /* File descriptors */
    int fd_count;               /* Number of FDs */
} unix_cmsg_t;

/* Unix socket state */
typedef struct unix_socket {
    int type;                           /* SOCK_STREAM or SOCK_DGRAM */
    char path[UNIX_PATH_MAX];           /* Bound filesystem path */
    bool bound;                         /* Bound to a path */
    bool listening;                     /* Listening (SOCK_STREAM) */
    bool connected;                     /* Connected (SOCK_STREAM) */

    /* SOCK_STREAM specific */
    struct unix_socket *peer;           /* Connected peer socket */
    unix_stream_buf_t *stream_buf;      /* Stream receive buffer */
    unix_conn_queue_entry_t *conn_queue_head;  /* Connection queue */
    unix_conn_queue_entry_t *conn_queue_tail;
    int backlog;                        /* Listen backlog */
    int conn_queue_len;                 /* Current queue length */

    /* SOCK_DGRAM specific */
    unix_dgram_entry_t *dgram_queue_head;   /* Datagram queue */
    unix_dgram_entry_t *dgram_queue_tail;
    int dgram_queue_len;                /* Current queue length */

    /* Control messages (for SCM_RIGHTS) */
    unix_cmsg_t *pending_cmsg;          /* Pending control message */

    spinlock_t lock;
} unix_socket_t;

/* Unix socket initialization */
void unix_socket_init(void);

/* Unix socket operations */
unix_socket_t *unix_socket_create(int type);
void unix_socket_destroy(unix_socket_t *sock);
int unix_socket_bind(unix_socket_t *sock, const char *path);
int unix_socket_connect(unix_socket_t *sock, const char *path);
int unix_socket_listen(unix_socket_t *sock, int backlog);
unix_socket_t *unix_socket_accept(unix_socket_t *sock);
int unix_socket_sendto(unix_socket_t *sock, const void *buf, size_t len, const char *dest_path);
ssize_t unix_socket_recvfrom(unix_socket_t *sock, void *buf, size_t len, char *src_path);

/* Control message operations (SCM_RIGHTS) */
int unix_socket_send_fds(unix_socket_t *sock, const int *fds, int fd_count);
int unix_socket_recv_fds(unix_socket_t *sock, int *fds, int *fd_count);

#endif /* _NET_UNIX_SOCKET_H */
