/* tty.c - Terminal device */

#include "tty.h"
#include "vga.h"
#include "../fs/vfs.h"
#include "../debug/debug.h"
#include "../lib/string.h"

/* Input buffer (circular) */
static char input_buf[TTY_BUF_SIZE];
static volatile uint32_t input_head = 0;  /* Write position */
static volatile uint32_t input_tail = 0;  /* Read position */
static volatile uint32_t input_count = 0;

/* TTY node operations */
static ssize_t tty_node_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset);
static ssize_t tty_node_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);

static node_ops_t tty_ops = {
    .read = tty_node_read,
    .write = tty_node_write,
};

/*
 * Write to TTY (VGA output)
 */
int64_t tty_write(const void *buf, size_t size) {
    const char *str = (const char *)buf;
    for (size_t i = 0; i < size; i++) {
        vga_putc(str[i]);
    }
    return (int64_t)size;
}

/*
 * Read from TTY input buffer
 */
int64_t tty_read(void *buf, size_t size) {
    char *dst = (char *)buf;
    size_t bytes_read = 0;

    /* If buffer empty, return EAGAIN (would block) */
    if (input_count == 0) {
        return -11;  /* EAGAIN */
    }

    /* Read available characters */
    while (bytes_read < size && input_count > 0) {
        dst[bytes_read++] = input_buf[input_tail];
        input_tail = (input_tail + 1) % TTY_BUF_SIZE;
        input_count--;
    }

    return (int64_t)bytes_read;
}

/*
 * Add character to input buffer (from keyboard IRQ)
 */
void tty_input_char(char c) {
    if (input_count >= TTY_BUF_SIZE) {
        return;  /* Buffer full, drop character */
    }

    input_buf[input_head] = c;
    input_head = (input_head + 1) % TTY_BUF_SIZE;
    input_count++;

    /* Echo to screen */
    vga_putc(c);
}

/*
 * VFS read callback
 */
static ssize_t tty_node_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    (void)node;
    (void)offset;
    return tty_read(buf, size);
}

/*
 * VFS write callback
 */
static ssize_t tty_node_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    (void)node;
    (void)offset;
    return tty_write(buf, size);
}

/*
 * Initialize TTY and create /dev/console
 */
void tty_init(void) {
    INFO("Initializing TTY subsystem...");

    /* Create /dev directory if it doesn't exist */
    vfs_mkdir("/dev");

    /* Create console device node */
    vfs_node_t *console = vfs_node_alloc();
    if (!console) {
        ERROR("Failed to allocate /dev/console node");
        return;
    }

    strcpy(console->name, "console");
    console->type = VFS_CHARDEV;
    console->ops = &tty_ops;
    console->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
    console->ref_count = 1;

    /* Find /dev and add console as child */
    vfs_node_t *dev = vfs_lookup("/dev");
    if (!dev) {
        ERROR("Failed to find /dev");
        vfs_node_free(console);
        return;
    }

    /* Add console to /dev */
    console->parent = dev;
    console->next = dev->children;
    if (dev->children) {
        dev->children->prev = console;
    }
    dev->children = console;

    INFO("TTY initialized: /dev/console");
}
