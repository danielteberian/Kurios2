/* unix_socket.c - Unix Domain Socket Implementation */

#include "unix_socket.h"
#include "socket.h"
#include "../debug/debug.h"
#include "../mm/slab.h"
#include "../lib/string.h"
#include "../fs/vfs.h"

/* Global table of bound Unix sockets */
#define MAX_UNIX_SOCKETS    256
static unix_socket_t *bound_sockets[MAX_UNIX_SOCKETS];
static spinlock_t unix_socket_table_lock = SPINLOCK_INIT;

/*
 * Initialize Unix socket subsystem
 */
void unix_socket_init(void) {
    memset(bound_sockets, 0, sizeof(bound_sockets));
    spin_init(&unix_socket_table_lock);
}

/*
 * Find bound Unix socket by path
 */
static unix_socket_t *unix_socket_find_by_path(const char *path) {
    uint64_t flags = spin_lock_irqsave(&unix_socket_table_lock);
    
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (bound_sockets[i] && strcmp(bound_sockets[i]->path, path) == 0) {
            unix_socket_t *sock = bound_sockets[i];
            spin_unlock_irqrestore(&unix_socket_table_lock, flags);
            return sock;
        }
    }
    
    spin_unlock_irqrestore(&unix_socket_table_lock, flags);
    return NULL;
}

/*
 * Register bound Unix socket
 */
static int unix_socket_register(unix_socket_t *sock) {
    uint64_t flags = spin_lock_irqsave(&unix_socket_table_lock);
    
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (!bound_sockets[i]) {
            bound_sockets[i] = sock;
            spin_unlock_irqrestore(&unix_socket_table_lock, flags);
            return 0;
        }
    }
    
    spin_unlock_irqrestore(&unix_socket_table_lock, flags);
    return -23;  /* ENFILE - too many open files */
}

/*
 * Unregister bound Unix socket
 */
static void unix_socket_unregister(unix_socket_t *sock) {
    uint64_t flags = spin_lock_irqsave(&unix_socket_table_lock);
    
    for (int i = 0; i < MAX_UNIX_SOCKETS; i++) {
        if (bound_sockets[i] == sock) {
            bound_sockets[i] = NULL;
            break;
        }
    }
    
    spin_unlock_irqrestore(&unix_socket_table_lock, flags);
}

/*
 * Create a Unix socket
 */
unix_socket_t *unix_socket_create(int type) {
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        return NULL;
    }

    unix_socket_t *sock = kmalloc(sizeof(unix_socket_t));
    if (!sock) return NULL;

    memset(sock, 0, sizeof(unix_socket_t));
    sock->type = type;
    sock->bound = false;
    sock->listening = false;
    sock->connected = false;
    spin_init(&sock->lock);

    /* Allocate stream buffer for SOCK_STREAM */
    if (type == SOCK_STREAM) {
        sock->stream_buf = kmalloc(sizeof(unix_stream_buf_t));
        if (!sock->stream_buf) {
            kfree(sock);
            return NULL;
        }
        memset(sock->stream_buf, 0, sizeof(unix_stream_buf_t));
    }

    return sock;
}

/*
 * Destroy a Unix socket
 */
void unix_socket_destroy(unix_socket_t *sock) {
    if (!sock) return;

    /* Unregister if bound */
    if (sock->bound) {
        unix_socket_unregister(sock);
        /* Remove socket file from filesystem */
        vfs_unlink(sock->path);
    }

    /* Disconnect peer */
    if (sock->peer) {
        uint64_t flags = spin_lock_irqsave(&sock->peer->lock);
        sock->peer->peer = NULL;
        sock->peer->connected = false;
        spin_unlock_irqrestore(&sock->peer->lock, flags);
    }

    /* Free connection queue */
    unix_conn_queue_entry_t *entry = sock->conn_queue_head;
    while (entry) {
        unix_conn_queue_entry_t *next = entry->next;
        kfree(entry);
        entry = next;
    }

    /* Free datagram queue */
    unix_dgram_entry_t *dg = sock->dgram_queue_head;
    while (dg) {
        unix_dgram_entry_t *next = dg->next;
        kfree(dg);
        dg = next;
    }

    /* Free buffers */
    if (sock->stream_buf) kfree(sock->stream_buf);
    if (sock->pending_cmsg) kfree(sock->pending_cmsg);

    kfree(sock);
}

/*
 * Bind Unix socket to filesystem path
 */
int unix_socket_bind(unix_socket_t *sock, const char *path) {
    if (!sock || !path) return -22;  /* EINVAL */

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (sock->bound) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -22;  /* Already bound */
    }

    /* Check if path already exists */
    if (unix_socket_find_by_path(path)) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -98;  /* EADDRINUSE */
    }

    /* Copy path */
    strncpy(sock->path, path, UNIX_PATH_MAX - 1);
    sock->path[UNIX_PATH_MAX - 1] = '\0';
    sock->bound = true;

    spin_unlock_irqrestore(&sock->lock, flags);

    /* Register in global table */
    int ret = unix_socket_register(sock);
    if (ret < 0) {
        sock->bound = false;
        return ret;
    }

    /* Create socket file in VFS (type = S_IFSOCK) */
    /* Note: This is a placeholder - real implementation would create proper socket inode */
    DEBUG("Unix socket bound to %s", path);

    return 0;
}

/*
 * Connect Unix socket to path (SOCK_STREAM or SOCK_DGRAM)
 */
int unix_socket_connect(unix_socket_t *sock, const char *path) {
    if (!sock || !path) return -22;

    /* Find target socket */
    unix_socket_t *target = unix_socket_find_by_path(path);
    if (!target) {
        return -2;  /* ENOENT */
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (sock->type == SOCK_STREAM) {
        /* Stream socket must connect to listening socket */
        if (!target->listening) {
            spin_unlock_irqrestore(&sock->lock, flags);
            return -111;  /* ECONNREFUSED */
        }

        /* Add to target's connection queue */
        uint64_t tflags = spin_lock_irqsave(&target->lock);

        if (target->conn_queue_len >= target->backlog) {
            spin_unlock_irqrestore(&target->lock, tflags);
            spin_unlock_irqrestore(&sock->lock, flags);
            return -11;  /* EAGAIN - queue full */
        }

        /* Create queue entry */
        unix_conn_queue_entry_t *entry = kmalloc(sizeof(unix_conn_queue_entry_t));
        if (!entry) {
            spin_unlock_irqrestore(&target->lock, tflags);
            spin_unlock_irqrestore(&sock->lock, flags);
            return -12;  /* ENOMEM */
        }

        entry->peer = sock;
        entry->next = NULL;

        /* Add to tail of queue */
        if (!target->conn_queue_head) {
            target->conn_queue_head = entry;
            target->conn_queue_tail = entry;
        } else {
            target->conn_queue_tail->next = entry;
            target->conn_queue_tail = entry;
        }
        target->conn_queue_len++;

        spin_unlock_irqrestore(&target->lock, tflags);

        /* Wait for accept() - for now, mark as connected immediately */
        sock->connected = true;
        strncpy(sock->path, path, UNIX_PATH_MAX - 1);

    } else {  /* SOCK_DGRAM */
        /* Just record the target path for sendto() */
        sock->connected = true;
        strncpy(sock->path, path, UNIX_PATH_MAX - 1);
    }

    spin_unlock_irqrestore(&sock->lock, flags);
    return 0;
}

/*
 * Listen for connections (SOCK_STREAM only)
 */
int unix_socket_listen(unix_socket_t *sock, int backlog) {
    if (!sock || sock->type != SOCK_STREAM) return -22;

    if (!sock->bound) {
        return -22;  /* Must be bound first */
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);
    sock->listening = true;
    sock->backlog = backlog;
    spin_unlock_irqrestore(&sock->lock, flags);

    DEBUG("Unix socket listening on %s (backlog=%d)", sock->path, backlog);
    return 0;
}

/*
 * Accept incoming connection (SOCK_STREAM only)
 */
unix_socket_t *unix_socket_accept(unix_socket_t *sock) {
    if (!sock || sock->type != SOCK_STREAM || !sock->listening) {
        return NULL;
    }

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    /* Check if any connections in queue */
    if (!sock->conn_queue_head) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return NULL;  /* No connections */
    }

    /* Dequeue first connection */
    unix_conn_queue_entry_t *entry = sock->conn_queue_head;
    sock->conn_queue_head = entry->next;
    if (!sock->conn_queue_head) {
        sock->conn_queue_tail = NULL;
    }
    sock->conn_queue_len--;

    unix_socket_t *client_sock = entry->peer;
    kfree(entry);

    spin_unlock_irqrestore(&sock->lock, flags);

    /* Create new socket for accepted connection */
    unix_socket_t *new_sock = unix_socket_create(SOCK_STREAM);
    if (!new_sock) {
        return NULL;
    }

    /* Connect the two sockets */
    new_sock->peer = client_sock;
    new_sock->connected = true;
    new_sock->bound = true;
    strncpy(new_sock->path, sock->path, UNIX_PATH_MAX - 1);

    uint64_t cflags = spin_lock_irqsave(&client_sock->lock);
    client_sock->peer = new_sock;
    client_sock->connected = true;
    spin_unlock_irqrestore(&client_sock->lock, cflags);

    DEBUG("Unix socket connection accepted");
    return new_sock;
}

/*
 * Send data via Unix socket
 */
int unix_socket_sendto(unix_socket_t *sock, const void *buf, size_t len, const char *dest_path) {
    if (!sock || !buf || len == 0) return -22;

    if (sock->type == SOCK_STREAM) {
        /* Stream socket - must be connected */
        if (!sock->connected || !sock->peer) {
            return -107;  /* ENOTCONN */
        }

        unix_socket_t *peer = sock->peer;
        uint64_t flags = spin_lock_irqsave(&peer->lock);

        /* Write to peer's receive buffer (circular) */
        unix_stream_buf_t *buf_p = peer->stream_buf;
        size_t written = 0;

        while (written < len && buf_p->count < UNIX_SOCK_BUF_SIZE) {
            buf_p->data[buf_p->write_pos] = ((uint8_t*)buf)[written];
            buf_p->write_pos = (buf_p->write_pos + 1) % UNIX_SOCK_BUF_SIZE;
            buf_p->count++;
            written++;
        }

        spin_unlock_irqrestore(&peer->lock, flags);

        if (written < len) {
            return -11;  /* EAGAIN - buffer full */
        }

        return (int)written;

    } else {  /* SOCK_DGRAM */
        /* Find target socket */
        const char *target_path = dest_path ? dest_path : sock->path;
        if (!target_path || target_path[0] == '\0') {
            return -89;  /* EDESTADDRREQ */
        }

        unix_socket_t *target = unix_socket_find_by_path(target_path);
        if (!target) {
            return -2;  /* ENOENT */
        }

        /* Create datagram entry */
        unix_dgram_entry_t *dg = kmalloc(sizeof(unix_dgram_entry_t));
        if (!dg) return -12;  /* ENOMEM */

        /* Copy data */
        size_t copy_len = len < UNIX_SOCK_BUF_SIZE ? len : UNIX_SOCK_BUF_SIZE;
        memcpy(dg->data, buf, copy_len);
        dg->len = copy_len;
        strncpy(dg->src_path, sock->bound ? sock->path : "", UNIX_PATH_MAX - 1);
        dg->next = NULL;

        /* Add to target's datagram queue */
        uint64_t flags = spin_lock_irqsave(&target->lock);

        if (!target->dgram_queue_head) {
            target->dgram_queue_head = dg;
            target->dgram_queue_tail = dg;
        } else {
            target->dgram_queue_tail->next = dg;
            target->dgram_queue_tail = dg;
        }
        target->dgram_queue_len++;

        spin_unlock_irqrestore(&target->lock, flags);

        return (int)copy_len;
    }
}

/*
 * Receive data from Unix socket
 */
ssize_t unix_socket_recvfrom(unix_socket_t *sock, void *buf, size_t len, char *src_path) {
    if (!sock || !buf) return -22;

    if (sock->type == SOCK_STREAM) {
        /* Stream socket */
        uint64_t flags = spin_lock_irqsave(&sock->lock);

        unix_stream_buf_t *buf_p = sock->stream_buf;
        if (buf_p->count == 0) {
            spin_unlock_irqrestore(&sock->lock, flags);
            return -11;  /* EAGAIN - no data */
        }

        /* Read from circular buffer */
        size_t read_count = 0;
        while (read_count < len && buf_p->count > 0) {
            ((uint8_t*)buf)[read_count] = buf_p->data[buf_p->read_pos];
            buf_p->read_pos = (buf_p->read_pos + 1) % UNIX_SOCK_BUF_SIZE;
            buf_p->count--;
            read_count++;
        }

        if (src_path && sock->peer) {
            strncpy(src_path, sock->peer->path, UNIX_PATH_MAX - 1);
        }

        spin_unlock_irqrestore(&sock->lock, flags);
        return (ssize_t)read_count;

    } else {  /* SOCK_DGRAM */
        uint64_t flags = spin_lock_irqsave(&sock->lock);

        /* Check if any datagrams in queue */
        if (!sock->dgram_queue_head) {
            spin_unlock_irqrestore(&sock->lock, flags);
            return -11;  /* EAGAIN - no data */
        }

        /* Dequeue first datagram */
        unix_dgram_entry_t *dg = sock->dgram_queue_head;
        sock->dgram_queue_head = dg->next;
        if (!sock->dgram_queue_head) {
            sock->dgram_queue_tail = NULL;
        }
        sock->dgram_queue_len--;

        spin_unlock_irqrestore(&sock->lock, flags);

        /* Copy data */
        size_t copy_len = len < dg->len ? len : dg->len;
        memcpy(buf, dg->data, copy_len);

        if (src_path) {
            strncpy(src_path, dg->src_path, UNIX_PATH_MAX - 1);
        }

        ssize_t ret = (ssize_t)copy_len;
        kfree(dg);
        return ret;
    }
}

/*
 * Send file descriptors via SCM_RIGHTS
 */
int unix_socket_send_fds(unix_socket_t *sock, const int *fds, int fd_count) {
    if (!sock || !fds || fd_count <= 0 || fd_count > CMSG_MAX_FDS) {
        return -22;  /* EINVAL */
    }

    if (sock->type != SOCK_STREAM || !sock->connected || !sock->peer) {
        return -107;  /* ENOTCONN */
    }

    /* Create control message */
    unix_cmsg_t *cmsg = kmalloc(sizeof(unix_cmsg_t));
    if (!cmsg) return -12;  /* ENOMEM */

    cmsg->type = SCM_RIGHTS;
    cmsg->fd_count = fd_count;
    for (int i = 0; i < fd_count; i++) {
        cmsg->fds[i] = fds[i];
    }

    /* Store in peer's pending control message */
    unix_socket_t *peer = sock->peer;
    uint64_t flags = spin_lock_irqsave(&peer->lock);

    if (peer->pending_cmsg) {
        spin_unlock_irqrestore(&peer->lock, flags);
        kfree(cmsg);
        return -11;  /* EAGAIN - previous message not consumed */
    }

    peer->pending_cmsg = cmsg;
    spin_unlock_irqrestore(&peer->lock, flags);

    DEBUG("Sent %d file descriptors via SCM_RIGHTS", fd_count);
    return 0;
}

/*
 * Receive file descriptors via SCM_RIGHTS
 */
int unix_socket_recv_fds(unix_socket_t *sock, int *fds, int *fd_count) {
    if (!sock || !fds || !fd_count) return -22;

    uint64_t flags = spin_lock_irqsave(&sock->lock);

    if (!sock->pending_cmsg) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return -61;  /* ENODATA - no control message */
    }

    unix_cmsg_t *cmsg = sock->pending_cmsg;
    *fd_count = cmsg->fd_count;
    for (int i = 0; i < cmsg->fd_count; i++) {
        fds[i] = cmsg->fds[i];
    }

    /* Clear pending message */
    sock->pending_cmsg = NULL;
    spin_unlock_irqrestore(&sock->lock, flags);

    kfree(cmsg);
    DEBUG("Received %d file descriptors via SCM_RIGHTS", *fd_count);
    return 0;
}
