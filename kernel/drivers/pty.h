/* pty.h - Pseudo-Terminal (PTY) Driver */
#ifndef _KERNEL_PTY_H
#define _KERNEL_PTY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "tty.h"
#include "../fs/vfs.h"

/* PTY buffer sizes */
#define PTY_BUF_SIZE    4096    /* Master-to-slave and slave-to-master buffers */
#define PTY_MAX         64      /* Maximum number of PTY pairs */

/*
 * PTY structure - represents a master/slave terminal pair
 *
 * Data flow:
 *   Terminal Emulator (master)              Shell/Program (slave)
 *           |                                       |
 *      write() ──────────────────────────────> read()
 *           |      [line discipline]                |
 *           |      (echo, signals, editing)         |
 *      read() <────────────────────────────── write()
 *           |      [raw output]                     |
 */
typedef struct pty {
    uint32_t number;            /* /dev/pts/N */
    bool allocated;             /* PTY slot in use */
    bool slave_locked;          /* TIOCSPTLCK state (slave can't be opened) */

    tty_t tty;                  /* Line discipline state (termios, buffers) */

    /*
     * Master-to-slave buffer (after line discipline processing)
     * Master write -> line discipline -> m2s_buf -> slave read
     */
    char m2s_buf[PTY_BUF_SIZE];
    volatile uint32_t m2s_head;
    volatile uint32_t m2s_tail;
    volatile uint32_t m2s_count;

    /*
     * Slave-to-master buffer (raw output)
     * Slave write -> output processing -> s2m_buf -> master read
     */
    char s2m_buf[PTY_BUF_SIZE];
    volatile uint32_t s2m_head;
    volatile uint32_t s2m_tail;
    volatile uint32_t s2m_count;

    /* Reference counts */
    uint32_t master_refs;       /* Open master file descriptors */
    uint32_t slave_refs;        /* Open slave file descriptors */

    /* VFS nodes */
    vfs_node_t *master_node;    /* Master device node (from ptmx open) */
    vfs_node_t *slave_node;     /* Slave device node (/dev/pts/N) */
} pty_t;

/*
 * Initialize PTY subsystem
 * Creates /dev/ptmx and /dev/pts/ directory
 */
void pty_init(void);

#endif /* _KERNEL_PTY_H */
