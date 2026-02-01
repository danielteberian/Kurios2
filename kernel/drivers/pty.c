/* pty.c - Pseudo-Terminal (PTY) Driver */

#include "pty.h"
#include "termios.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "../signal/signal.h"
#include "../mm/slab.h"

/* PTY table - static array of PTY slots */
static pty_t pty_table[PTY_MAX];

/* /dev/pts directory node */
static vfs_node_t *pts_dir = NULL;

/* Forward declarations */
static int ptmx_open(vfs_node_t *node, uint32_t flags);
static ssize_t pty_master_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset);
static ssize_t pty_master_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);
static int pty_master_ioctl(vfs_node_t *node, unsigned long request, void *arg);
static void pty_master_close(vfs_node_t *node);

static int pty_slave_open(vfs_node_t *node, uint32_t flags);
static ssize_t pty_slave_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset);
static ssize_t pty_slave_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);
static int pty_slave_ioctl(vfs_node_t *node, unsigned long request, void *arg);
static void pty_slave_close(vfs_node_t *node);

/* /dev/ptmx operations */
static node_ops_t ptmx_ops = {
    .open = ptmx_open,
    .close = NULL,
    .read = NULL,
    .write = NULL,
    .ioctl = NULL,
};

/* PTY master operations */
static node_ops_t pty_master_ops = {
    .open = NULL,
    .close = pty_master_close,
    .read = pty_master_read,
    .write = pty_master_write,
    .ioctl = pty_master_ioctl,
};

/* PTY slave operations */
static node_ops_t pty_slave_ops = {
    .open = pty_slave_open,
    .close = pty_slave_close,
    .read = pty_slave_read,
    .write = pty_slave_write,
    .ioctl = pty_slave_ioctl,
};

/*
 * Allocate a new PTY slot
 * Returns PTY number on success, -1 on failure
 */
static int pty_alloc(void) {
    for (uint32_t i = 0; i < PTY_MAX; i++) {
        if (!pty_table[i].allocated) {
            pty_table[i].allocated = true;
            pty_table[i].number = i;
            pty_table[i].slave_locked = true;  /* Locked by default */
            pty_table[i].master_refs = 0;
            pty_table[i].slave_refs = 0;
            pty_table[i].master_node = NULL;
            pty_table[i].slave_node = NULL;

            /* Initialize buffers */
            pty_table[i].m2s_head = 0;
            pty_table[i].m2s_tail = 0;
            pty_table[i].m2s_count = 0;
            pty_table[i].s2m_head = 0;
            pty_table[i].s2m_tail = 0;
            pty_table[i].s2m_count = 0;

            /* Initialize TTY state */
            memset(&pty_table[i].tty, 0, sizeof(tty_t));
            tty_init_termios(&pty_table[i].tty.termios);

            /* Default window size */
            pty_table[i].tty.winsize.ws_row = 24;
            pty_table[i].tty.winsize.ws_col = 80;

            return (int)i;
        }
    }
    return -1;  /* No free PTY slots */
}

/*
 * Free a PTY slot
 */
static void pty_free(pty_t *pty) {
    if (!pty || !pty->allocated) return;

    /* Remove slave node from /dev/pts if present */
    if (pty->slave_node && pts_dir) {
        /* Unlink from directory */
        vfs_node_t **pp = &pts_dir->children;
        while (*pp) {
            if (*pp == pty->slave_node) {
                *pp = pty->slave_node->next;
                if (pty->slave_node->next) {
                    pty->slave_node->next->prev = pty->slave_node->prev;
                }
                break;
            }
            pp = &(*pp)->next;
        }
        vfs_node_free(pty->slave_node);
        pty->slave_node = NULL;
    }

    if (pty->master_node) {
        vfs_node_free(pty->master_node);
        pty->master_node = NULL;
    }

    pty->allocated = false;
    DEBUG("pty_free: freed PTY %u", pty->number);
}

/*
 * PTY output callback for line discipline (goes to m2s buffer)
 */
static void pty_output_cb(void *ctx, char c) {
    pty_t *pty = (pty_t *)ctx;

    if (pty->m2s_count < PTY_BUF_SIZE) {
        pty->m2s_buf[pty->m2s_head] = c;
        pty->m2s_head = (pty->m2s_head + 1) % PTY_BUF_SIZE;
        pty->m2s_count++;
    }
}

/*
 * PTY echo callback for line discipline (goes to s2m buffer for master to read)
 */
static void pty_echo_cb(void *ctx, char c) {
    pty_t *pty = (pty_t *)ctx;

    /* Output processing for echo */
    if (pty->tty.termios.c_oflag & OPOST) {
        if ((pty->tty.termios.c_oflag & ONLCR) && c == '\n') {
            /* Translate NL to CR-NL */
            if (pty->s2m_count < PTY_BUF_SIZE) {
                pty->s2m_buf[pty->s2m_head] = '\r';
                pty->s2m_head = (pty->s2m_head + 1) % PTY_BUF_SIZE;
                pty->s2m_count++;
            }
        }
    }

    if (pty->s2m_count < PTY_BUF_SIZE) {
        pty->s2m_buf[pty->s2m_head] = c;
        pty->s2m_head = (pty->s2m_head + 1) % PTY_BUF_SIZE;
        pty->s2m_count++;
    }
}

/*
 * PTY signal callback (sends signal to foreground process group)
 */
static void pty_signal_cb(void *ctx, int signum) {
    pty_t *pty = (pty_t *)ctx;

    if (pty->tty.fg_pgrp == 0) {
        DEBUG("pty_signal_cb: no foreground process group for PTY %u", pty->number);
        return;
    }

    DEBUG("PTY %u sending signal %d to pgrp %u", pty->number, signum, pty->tty.fg_pgrp);
    signal_send(pty->tty.fg_pgrp, signum);
}

/*
 * Create slave device node /dev/pts/N
 */
static vfs_node_t *pty_create_slave_node(pty_t *pty) {
    if (!pts_dir) return NULL;

    vfs_node_t *node = vfs_node_alloc();
    if (!node) return NULL;

    /* Name: just the number */
    char name[16];
    int len = 0;
    uint32_t n = pty->number;
    if (n == 0) {
        name[len++] = '0';
    } else {
        char tmp[16];
        int i = 0;
        while (n > 0) {
            tmp[i++] = '0' + (n % 10);
            n /= 10;
        }
        while (i > 0) {
            name[len++] = tmp[--i];
        }
    }
    name[len] = '\0';
    strcpy(node->name, name);

    node->type = VFS_CHARDEV;
    node->ops = &pty_slave_ops;
    node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
    node->ref_count = 1;
    node->private = pty;

    /* Add to /dev/pts */
    node->parent = pts_dir;
    node->next = pts_dir->children;
    if (pts_dir->children) {
        pts_dir->children->prev = node;
    }
    pts_dir->children = node;

    return node;
}

/*
 * /dev/ptmx open - allocates a new PTY pair
 */
static int ptmx_open(vfs_node_t *node, uint32_t flags) {
    (void)flags;

    /* Allocate new PTY */
    int pty_num = pty_alloc();
    if (pty_num < 0) {
        DEBUG("ptmx_open: no free PTY slots");
        return -12;  /* ENOMEM */
    }

    pty_t *pty = &pty_table[pty_num];

    /* Create master node (this is what gets returned to caller) */
    vfs_node_t *master = vfs_node_alloc();
    if (!master) {
        pty_free(pty);
        return -12;
    }

    strcpy(master->name, "ptm");
    master->type = VFS_CHARDEV;
    master->ops = &pty_master_ops;
    master->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
    master->ref_count = 1;
    master->private = pty;

    pty->master_node = master;
    pty->master_refs = 1;

    /* Create slave node /dev/pts/N */
    pty->slave_node = pty_create_slave_node(pty);
    if (!pty->slave_node) {
        vfs_node_free(master);
        pty_free(pty);
        return -12;
    }

    /* Replace the node in the open file with master node */
    /* This is a bit of a hack - we swap the node after open */
    node->private = master;  /* Store master node to be retrieved */

    DEBUG("ptmx_open: allocated PTY %u", pty_num);
    return 0;
}

/*
 * Master read - returns data from s2m buffer (slave output)
 */
static ssize_t pty_master_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    (void)offset;

    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return -9;  /* EBADF */

    char *dst = (char *)buf;
    size_t bytes_read = 0;

    /* If no slave refs and no data, return EOF */
    if (pty->slave_refs == 0 && pty->s2m_count == 0) {
        return 0;  /* EOF */
    }

    /* Read from s2m buffer */
    if (pty->s2m_count == 0) {
        return -11;  /* EAGAIN */
    }

    while (bytes_read < size && pty->s2m_count > 0) {
        dst[bytes_read++] = pty->s2m_buf[pty->s2m_tail];
        pty->s2m_tail = (pty->s2m_tail + 1) % PTY_BUF_SIZE;
        pty->s2m_count--;
    }

    return (ssize_t)bytes_read;
}

/*
 * Master write - input goes through line discipline to slave
 */
static ssize_t pty_master_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    (void)offset;

    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return -9;  /* EBADF */

    /* If no slave refs, return EPIPE */
    if (pty->slave_refs == 0) {
        return -32;  /* EPIPE */
    }

    const char *src = (const char *)buf;

    /* Process each character through line discipline */
    for (size_t i = 0; i < size; i++) {
        tty_ldisc_input(&pty->tty, src[i],
                        pty_output_cb, pty_echo_cb, pty_signal_cb, pty);
    }

    return (ssize_t)size;
}

/*
 * Master ioctl
 */
static int pty_master_ioctl(vfs_node_t *node, unsigned long request, void *arg) {
    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return -9;  /* EBADF */

    switch (request) {
        case TIOCGPTN:
            /* Get PTY number */
            if (!arg) return -22;
            *(uint32_t *)arg = pty->number;
            return 0;

        case TIOCSPTLCK: {
            /* Lock/unlock slave */
            if (!arg) return -22;
            int lock = *(int *)arg;
            pty->slave_locked = (lock != 0);
            DEBUG("PTY %u slave %s", pty->number, pty->slave_locked ? "locked" : "unlocked");
            return 0;
        }

        default:
            /* Try common TTY ioctls */
            return tty_ioctl_common(&pty->tty, request, arg);
    }
}

/*
 * Master close
 */
static void pty_master_close(vfs_node_t *node) {
    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return;

    pty->master_refs--;
    DEBUG("pty_master_close: PTY %u master_refs=%u", pty->number, pty->master_refs);

    if (pty->master_refs == 0) {
        /* Send SIGHUP to foreground process group */
        if (pty->tty.fg_pgrp != 0) {
            DEBUG("PTY %u sending SIGHUP to pgrp %u", pty->number, pty->tty.fg_pgrp);
            signal_send(pty->tty.fg_pgrp, SIGHUP);
        }

        /* If slave also closed, free the PTY */
        if (pty->slave_refs == 0) {
            pty_free(pty);
        }
    }
}

/*
 * Slave open
 */
static int pty_slave_open(vfs_node_t *node, uint32_t flags) {
    (void)flags;

    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return -2;  /* ENOENT */

    /* Check if slave is locked */
    if (pty->slave_locked) {
        DEBUG("pty_slave_open: PTY %u slave is locked", pty->number);
        return -13;  /* EACCES */
    }

    /* Check if master is still open */
    if (pty->master_refs == 0) {
        DEBUG("pty_slave_open: PTY %u master is closed", pty->number);
        return -5;  /* EIO */
    }

    pty->slave_refs++;
    DEBUG("pty_slave_open: PTY %u slave_refs=%u", pty->number, pty->slave_refs);

    return 0;
}

/*
 * Slave read - returns data from m2s buffer or canon_buf
 */
static ssize_t pty_slave_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    (void)offset;

    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return -9;  /* EBADF */

    char *dst = (char *)buf;
    size_t bytes_read = 0;

    /* If master closed, return EOF */
    if (pty->master_refs == 0 && pty->tty.canon_len == 0 && pty->m2s_count == 0) {
        return 0;  /* EOF */
    }

    if (pty->tty.termios.c_lflag & ICANON) {
        /* Canonical mode: read from canon_buf */
        if (!pty->tty.canon_ready) {
            return -11;  /* EAGAIN */
        }

        while (bytes_read < size && bytes_read < pty->tty.canon_len) {
            dst[bytes_read] = pty->tty.canon_buf[bytes_read];
            bytes_read++;
        }

        /* Remove copied data */
        if (bytes_read == pty->tty.canon_len) {
            pty->tty.canon_len = 0;
            pty->tty.canon_ready = false;
        } else {
            memmove(pty->tty.canon_buf, pty->tty.canon_buf + bytes_read,
                    pty->tty.canon_len - bytes_read);
            pty->tty.canon_len -= bytes_read;
        }
    } else {
        /* Non-canonical mode: read from input_buf */
        if (pty->tty.input_count == 0) {
            uint8_t vmin = pty->tty.termios.c_cc[VMIN];
            if (vmin == 0) return 0;
            return -11;  /* EAGAIN */
        }

        while (bytes_read < size && pty->tty.input_count > 0) {
            dst[bytes_read++] = pty->tty.input_buf[pty->tty.input_tail];
            pty->tty.input_tail = (pty->tty.input_tail + 1) % TTY_INPUT_SIZE;
            pty->tty.input_count--;
        }
    }

    return (ssize_t)bytes_read;
}

/*
 * Slave write - output goes to s2m buffer with OPOST processing
 */
static ssize_t pty_slave_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    (void)offset;

    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return -9;  /* EBADF */

    /* If master closed, return EPIPE */
    if (pty->master_refs == 0) {
        return -32;  /* EPIPE */
    }

    const char *src = (const char *)buf;
    size_t bytes_written = 0;

    for (size_t i = 0; i < size; i++) {
        char c = src[i];

        /* Output processing (c_oflag) */
        if (pty->tty.termios.c_oflag & OPOST) {
            if ((pty->tty.termios.c_oflag & ONLCR) && c == '\n') {
                /* Translate NL to CR-NL */
                if (pty->s2m_count < PTY_BUF_SIZE) {
                    pty->s2m_buf[pty->s2m_head] = '\r';
                    pty->s2m_head = (pty->s2m_head + 1) % PTY_BUF_SIZE;
                    pty->s2m_count++;
                } else {
                    break;  /* Buffer full */
                }
            }
        }

        if (pty->s2m_count < PTY_BUF_SIZE) {
            pty->s2m_buf[pty->s2m_head] = c;
            pty->s2m_head = (pty->s2m_head + 1) % PTY_BUF_SIZE;
            pty->s2m_count++;
            bytes_written++;
        } else {
            break;  /* Buffer full */
        }
    }

    return (ssize_t)bytes_written;
}

/*
 * Slave ioctl
 */
static int pty_slave_ioctl(vfs_node_t *node, unsigned long request, void *arg) {
    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return -9;  /* EBADF */

    /* Try common TTY ioctls */
    int ret = tty_ioctl_common(&pty->tty, request, arg);
    if (ret != 1) return ret;

    /* Unknown ioctl */
    DEBUG("pty_slave_ioctl: unknown request 0x%lx", request);
    return -22;  /* EINVAL */
}

/*
 * Slave close
 */
static void pty_slave_close(vfs_node_t *node) {
    pty_t *pty = (pty_t *)node->private;
    if (!pty || !pty->allocated) return;

    pty->slave_refs--;
    DEBUG("pty_slave_close: PTY %u slave_refs=%u", pty->number, pty->slave_refs);

    /* If master also closed, free the PTY */
    if (pty->master_refs == 0 && pty->slave_refs == 0) {
        pty_free(pty);
    }
}

/*
 * Initialize PTY subsystem
 */
void pty_init(void) {
    INFO("Initializing PTY subsystem...");

    /* Initialize PTY table */
    memset(pty_table, 0, sizeof(pty_table));

    /* Create /dev/pts directory */
    vfs_mkdir("/dev/pts");
    pts_dir = vfs_lookup("/dev/pts");
    if (!pts_dir) {
        ERROR("Failed to create /dev/pts directory");
        return;
    }

    /* Create /dev/ptmx device */
    vfs_node_t *dev = vfs_lookup("/dev");
    if (!dev) {
        ERROR("Failed to find /dev");
        vfs_node_unref(pts_dir);
        pts_dir = NULL;
        return;
    }

    vfs_node_t *ptmx = vfs_node_alloc();
    if (!ptmx) {
        ERROR("Failed to allocate /dev/ptmx node");
        vfs_node_unref(dev);
        vfs_node_unref(pts_dir);
        pts_dir = NULL;
        return;
    }

    strcpy(ptmx->name, "ptmx");
    ptmx->type = VFS_CHARDEV;
    ptmx->ops = &ptmx_ops;
    ptmx->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
    ptmx->ref_count = 1;

    /* Add to /dev */
    ptmx->parent = dev;
    ptmx->next = dev->children;
    if (dev->children) {
        dev->children->prev = ptmx;
    }
    dev->children = ptmx;

    vfs_node_unref(dev);

    INFO("PTY initialized: /dev/ptmx, /dev/pts/ (max %d PTYs)", PTY_MAX);
}
