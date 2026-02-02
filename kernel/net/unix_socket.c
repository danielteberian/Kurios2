/* unix_socket.c - Unix Domain Socket Implementation */

#include "unix_socket.h"
#include "socket.h"
#include "../mm/slab.h"
#include "../lib/string.h"
#include "../fs/vfs.h"
#include "../debug/debug.h"

/* Unix socket table */
#define MAX_UNIX_SOCKETS    256
static unix_socket_t *unix_sockets[MAX_UNIX_SOCKETS];
static spinlock_t unix_socket_table_lock = SPINLOCK_INIT;

/*
 * Find Unix socket by path
 */
unix_socket_t *unix_socket_find_by_path(const char *path) {
    uint64_t flags = spin_lock_irqsave(&unix_socket_table_lock);

    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (unix_sockets[i] && strcmp(unix_sockets[i]->path, path) == 0) {
            unix_socket_t *sock = unix_sockets[i];
            spin_unlock_irqrestore(&unix_socket_table_lock, flags);
            return sock;
        }
    }

    spin_unlock_irqrestore(&unix_socket_table_lock, flags);
    return NULL;
}

/*
 * Create a Unix domain socket
 */
unix_socket_t *unix_socket_create(int type) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        return NULL;
    }

    unix_socket_t *sock = kmalloc(sizeof(unix_socket_t));
    if (!sock) {
        return NULL;
    }

    memset(sock, 0, sizeof(unix_socket_t));
    sock->type = type;
    sock->state = UNIX_CLOSED;
    sock->buf_size = UNIX_SOCKET_BUF_SIZE;
    sock->buffer = kmalloc(sock->buf_size);

    if (!sock->buffer) {
        kfree(sock);
        return NULL;
    }

    spin_init(&sock->lock);

    /* Add to socket table */
    uint64_t flags = spin_lock_irqsave(&unix_socket_table_lock);
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (!unix_sockets[i]) {
            unix_sockets[i] = sock;
            spin_unlock_irqrestore(&unix_socket_table_lock, flags);
            return sock;
        }
    }
    spin_unlock_irqrestore(&unix_socket_table_lock, flags);

    /* No free slots */
    kfree(sock->buffer);
    kfree(sock);
    return NULL;
}

/*
 * Destroy a Unix domain socket
 */
void unix_socket_destroy(unix_socket_t *sock) {
    if (!sock) return;

    /* Remove from socket table */
    uint64_t flags = spin_lock_irqsave(&unix_socket_table_lock);
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (unix_sockets[i] == sock) {
            unix_sockets[i] = NULL;
            break;
        }
    }
    spin_unlock_irqrestore(&unix_socket_table_lock, flags);

    /* Disconnect peer if connected */
    if (sock->peer) {
        uint64_t peer_flags = spin_lock_irqsave(&sock->peer->lock);
        sock->peer->peer = NULL;
        sock->peer->state = UNIX_CLOSED;
        spin_unlock_irqrestore(&sock->peer->lock, peer_flags);
    }

    /* Unlink socket file if bound */
    if (sock->state == UNIX_BOUND || sock->state == UNIX_LISTENING) {
        if (sock->path[0] != '\0') {
            vfs_unlink(sock->path);
        }
    }

    /* Free connection queue */
    if (sock->conn_queue) {
        kfree(sock->conn_queue);
    }

    /* Free buffer */
    if (sock->buffer) {
        kfree(sock->buffer);
    }

    kfree(sock);
}

/*
 * Bind Unix socket to a path
 */
int unix_socket_bind(unix_socket_t *sock, const char *path) {
    if (!sock || !path) {
        return -22;  /* EINVAL */
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (sock->state != UNIX_CLOSED) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -22;  /* EINVAL: already bound */
    }

    /* Copy path */
    strncpy(sock->path, path, UNIX_PATH_MAX - 1);
    sock->path[UNIX_PATH_MAX - 1] = '\0';

    /* Create socket file in VFS */
    /* For now, just mark as bound - VFS socket file creation would go here */
    sock->state = UNIX_BOUND;

    spin_unlock_irqrestore(&sock->lock, flags);
    return 0;
}

/*
 * Connect Unix socket to a path
 */
int unix_socket_connect(unix_socket_t *sock, const char *path) {
    if (!sock || !path) {
        return -22;  /* EINVAL */
    }

    if (sock->type == SOCK_DGRAM) {
        /* For DGRAM, just store the path */
        uint64_t flags = spin_lock_irqsave(&sock->lock);
        strncpy(sock->path, path, UNIX_PATH_MAX - 1);
        sock->path[UNIX_PATH_MAX - 1] = '\0';
        sock->state = UNIX_CONNECTED;
        spin_unlock_irqrestore(&sock->lock, flags);
        return 0;
    }

    /* For SOCK_STREAM, find the listening socket */
    unix_socket_t *listener = unix_socket_find_by_path(path);
    if (!listener) {
        return -2;  /* ENOENT */
    }

    uint64_t list_flags = spin_lock_irqsave(&listener->lock);

    if (listener->state != UNIX_LISTENING) {
        spin_unlock_irqrestore(&listener->lock, list_flags);
        return -111;  /* ECONNREFUSED */
    }

    /* Check if connection queue is full */
    if (!listener->conn_queue ||
        listener->conn_queue->count >= listener->conn_queue->backlog) {
        spin_unlock_irqrestore(&listener->lock, list_flags);
        return -11;  /* EAGAIN */
    }

    /* Add to connection queue */
    listener->conn_queue->pending[listener->conn_queue->count++] = sock;

    uint64_t sock_flags = spin_lock_irqsave(&sock->lock);
    sock->listener = listener;
    sock->state = UNIX_CONNECTED;
    spin_unlock_irqrestore(&sock->lock, sock_flags);

    spin_unlock_irqrestore(&listener->lock, list_flags);
    return 0;
}

/*
 * Listen on Unix socket
 */
int unix_socket_listen(unix_socket_t *sock, int backlog) {
    if (!sock || sock->type != SOCK_STREAM) {
        return -95;  /* EOPNOTSUPP */
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (sock->state != UNIX_BOUND) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -22;  /* EINVAL */
    }

    /* Allocate connection queue */
    if (!sock->conn_queue) {
        sock->conn_queue = kmalloc(sizeof(unix_connection_queue_t));
        if (!sock->conn_queue) {
            spin_unlock_irqrestore(&sock->lock, flags);
            return -12;  /* ENOMEM */
        }
        memset(sock->conn_queue, 0, sizeof(unix_connection_queue_t));
    }

    sock->conn_queue->backlog = (backlog > UNIX_CONN_QUEUE_SIZE) ?
                                  UNIX_CONN_QUEUE_SIZE : backlog;
    sock->state = UNIX_LISTENING;

    spin_unlock_irqrestore(&sock->lock, flags);
    return 0;
}

/*
 * Accept connection on Unix socket
 */
unix_socket_t *unix_socket_accept(unix_socket_t *sock) {
    if (!sock || sock->type != SOCK_STREAM || sock->state != UNIX_LISTENING) {
        return NULL;
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (!sock->conn_queue || sock->conn_queue->count == 0) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return NULL;  /* Would block */
    }

    /* Get first pending connection */
    unix_socket_t *client = sock->conn_queue->pending[0];

    /* Shift queue */
    for (int i = 1; i < sock->conn_queue->count; i++) {
        sock->conn_queue->pending[i - 1] = sock->conn_queue->pending[i];
    }
    sock->conn_queue->count--;

    /* Create new socket for the connection */
    unix_socket_t *conn_sock = unix_socket_create(SOCK_STREAM);
    if (!conn_sock) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return NULL;
    }

    /* Connect the two sockets */
    uint64_t conn_flags = spin_lock_irqsave(&conn_sock->lock);
    conn_sock->peer = client;
    conn_sock->state = UNIX_CONNECTED;
    spin_unlock_irqrestore(&conn_sock->lock, conn_flags);

    uint64_t client_flags = spin_lock_irqsave(&client->lock);
    client->peer = conn_sock;
    client->listener = NULL;
    spin_unlock_irqrestore(&client->lock, client_flags);

    spin_unlock_irqrestore(&sock->lock, flags);
    return conn_sock;
}

/*
 * Send data on Unix socket
 */
ssize_t unix_socket_send(unix_socket_t *sock, const void *buf, size_t len) {
    if (!sock || !buf || len == 0) {
        return -22;  /* EINVAL */
    }

    if (sock->type == SOCK_STREAM && sock->state != UNIX_CONNECTED) {
        return -107;  /* ENOTCONN */
    }

    unix_socket_t *target = NULL;

    if (sock->type == SOCK_STREAM) {
        /* Send to connected peer */
        target = sock->peer;
        if (!target) {
            return -32;  /* EPIPE */
        }
    } else {
        /* SOCK_DGRAM: find target by path */
        if (sock->path[0] == '\0') {
            return -89;  /* EDESTADDRREQ */
        }
        target = unix_socket_find_by_path(sock->path);
        if (!target) {
            return -111;  /* ECONNREFUSED */
        }
    }

    /* Write to target's buffer */
    uint64_t flags = spin_lock_irqsave(&target->lock);

    size_t space = target->buf_size - target->buf_count;
    if (space == 0) {
        spin_unlock_irqrestore(&target->lock, flags);
        return -11;  /* EAGAIN */
    }

    size_t to_write = (len > space) ? space : len;
    const uint8_t *src = buf;

    for (size_t i = 0; i < to_write; i++) {
        target->buffer[target->buf_head] = src[i];
        target->buf_head = (target->buf_head + 1) % target->buf_size;
        target->buf_count++;
    }

    spin_unlock_irqrestore(&target->lock, flags);
    return to_write;
}

/*
 * Receive data from Unix socket
 */
ssize_t unix_socket_recv(unix_socket_t *sock, void *buf, size_t len) {
    if (!sock || !buf || len == 0) {
        return -22;  /* EINVAL */
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (sock->buf_count == 0) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -11;  /* EAGAIN */
    }

    size_t to_read = (len > sock->buf_count) ? sock->buf_count : len;
    uint8_t *dst = buf;

    for (size_t i = 0; i < to_read; i++) {
        dst[i] = sock->buffer[sock->buf_tail];
        sock->buf_tail = (sock->buf_tail + 1) % sock->buf_size;
        sock->buf_count--;
    }

    spin_unlock_irqrestore(&sock->lock, flags);
    return to_read;
}
