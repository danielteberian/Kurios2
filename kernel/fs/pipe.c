/* pipe.c - Pipe implementation */

#include "pipe.h"
#include "vfs.h"
#include "fd_table.h"
#include "../mm/slab.h"
#include "../debug/debug.h"
#include "../process/process.h"
#include "../lib/string.h"

/* Pipe node operations */
static ssize_t pipe_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset);
static ssize_t pipe_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);
static void pipe_close(vfs_node_t *node);

static node_ops_t pipe_read_ops = {
    .read = pipe_read,
    .close = pipe_close,
};

static node_ops_t pipe_write_ops = {
    .write = pipe_write,
    .close = pipe_close,
};

/*
 * Read from pipe
 */
static ssize_t pipe_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    (void)offset;  /* Pipes don't use offset */

    if (!node || !node->private) {
        return -VFS_EINVAL;
    }

    pipe_t *pipe = (pipe_t *)node->private;
    uint8_t *dst = (uint8_t *)buf;
    size_t bytes_read = 0;

    /* If buffer is empty and no writers, return 0 (EOF) */
    if (pipe->count == 0 && pipe->writers == 0) {
        return 0;
    }

    /* If buffer is empty but writers exist, would block - return EAGAIN */
    if (pipe->count == 0) {
        return -11;  /* EAGAIN */
    }

    /* Read available data */
    while (bytes_read < size && pipe->count > 0) {
        dst[bytes_read++] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUF_SIZE;
        pipe->count--;
    }

    return (ssize_t)bytes_read;
}

/*
 * Write to pipe
 */
static ssize_t pipe_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    (void)offset;  /* Pipes don't use offset */

    if (!node || !node->private) {
        return -VFS_EINVAL;
    }

    pipe_t *pipe = (pipe_t *)node->private;
    const uint8_t *src = (const uint8_t *)buf;
    size_t bytes_written = 0;

    /* If no readers, return EPIPE */
    if (pipe->readers == 0) {
        return -32;  /* EPIPE */
    }

    /* If buffer is full, would block - return EAGAIN */
    if (pipe->count >= PIPE_BUF_SIZE) {
        return -11;  /* EAGAIN */
    }

    /* Write data to buffer */
    while (bytes_written < size && pipe->count < PIPE_BUF_SIZE) {
        pipe->buffer[pipe->write_pos] = src[bytes_written++];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUF_SIZE;
        pipe->count++;
    }

    return (ssize_t)bytes_written;
}

/*
 * Close pipe end
 */
static void pipe_close(vfs_node_t *node) {
    if (!node || !node->private) {
        return;
    }

    pipe_t *pipe = (pipe_t *)node->private;

    /* Decrement reader/writer count based on which end this is */
    if (node->ops == &pipe_read_ops) {
        if (pipe->readers > 0) {
            pipe->readers--;
        }
    } else {
        if (pipe->writers > 0) {
            pipe->writers--;
        }
    }

    /* Free pipe when both ends are closed */
    if (pipe->readers == 0 && pipe->writers == 0) {
        kfree(pipe);
        node->private = NULL;
    }
}

/*
 * Create a pipe
 */
int pipe_create(int *read_fd, int *write_fd) {
    process_t *proc = process_current();
    if (!proc || !proc->fd_table) {
        ERROR("pipe_create: No current process or fd table");
        return -VFS_EINVAL;
    }

    /* Allocate pipe structure */
    pipe_t *pipe = kmalloc(sizeof(pipe_t));
    if (!pipe) {
        ERROR("pipe_create: Failed to allocate pipe");
        return -VFS_ENOMEM;
    }

    memset(pipe, 0, sizeof(pipe_t));
    pipe->readers = 1;
    pipe->writers = 1;

    /* Allocate read-end node */
    vfs_node_t *read_node = vfs_node_alloc();
    if (!read_node) {
        kfree(pipe);
        ERROR("pipe_create: Failed to allocate read node");
        return -VFS_ENOMEM;
    }

    strcpy(read_node->name, "pipe");
    read_node->type = VFS_PIPE;
    read_node->ops = &pipe_read_ops;
    read_node->private = pipe;
    read_node->permissions = VFS_PERM_READ;
    read_node->ref_count = 1;

    /* Allocate write-end node */
    vfs_node_t *write_node = vfs_node_alloc();
    if (!write_node) {
        vfs_node_free(read_node);
        kfree(pipe);
        ERROR("pipe_create: Failed to allocate write node");
        return -VFS_ENOMEM;
    }

    strcpy(write_node->name, "pipe");
    write_node->type = VFS_PIPE;
    write_node->ops = &pipe_write_ops;
    write_node->private = pipe;
    write_node->permissions = VFS_PERM_WRITE;
    write_node->ref_count = 1;

    /* Create file structures */
    file_t *read_file = kmalloc(sizeof(file_t));
    file_t *write_file = kmalloc(sizeof(file_t));

    if (!read_file || !write_file) {
        if (read_file) kfree(read_file);
        if (write_file) kfree(write_file);
        vfs_node_free(write_node);
        vfs_node_free(read_node);
        kfree(pipe);
        ERROR("pipe_create: Failed to allocate file structures");
        return -VFS_ENOMEM;
    }

    memset(read_file, 0, sizeof(file_t));
    read_file->node = read_node;
    read_file->flags = O_RDONLY;
    read_file->ref_count = 1;

    memset(write_file, 0, sizeof(file_t));
    write_file->node = write_node;
    write_file->flags = O_WRONLY;
    write_file->ref_count = 1;

    /* Allocate file descriptors */
    int rfd = fd_table_alloc(proc->fd_table, read_file, 0);
    if (rfd < 0) {
        kfree(write_file);
        kfree(read_file);
        vfs_node_free(write_node);
        vfs_node_free(read_node);
        kfree(pipe);
        ERROR("pipe_create: Failed to allocate read fd");
        return -VFS_EMFILE;
    }

    int wfd = fd_table_alloc(proc->fd_table, write_file, 0);
    if (wfd < 0) {
        fd_table_free(proc->fd_table, rfd);
        kfree(write_file);
        kfree(read_file);
        vfs_node_free(write_node);
        vfs_node_free(read_node);
        kfree(pipe);
        ERROR("pipe_create: Failed to allocate write fd");
        return -VFS_EMFILE;
    }

    *read_fd = rfd;
    *write_fd = wfd;

    DEBUG("pipe_create: created pipe read_fd=%d, write_fd=%d", rfd, wfd);

    return 0;
}

/*
 * Initialize pipe subsystem
 */
void pipe_init(void) {
    INFO("Pipe subsystem initialized");
}
