/* tty.h - Terminal device */
#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* TTY input buffer size */
#define TTY_BUF_SIZE    256

/* Initialize TTY subsystem and create /dev/console */
void tty_init(void);

/* Write to TTY (called from VFS) */
int64_t tty_write(const void *buf, size_t size);

/* Read from TTY (called from VFS) */
int64_t tty_read(void *buf, size_t size);

/* Add character to TTY input buffer (called from keyboard IRQ) */
void tty_input_char(char c);

#endif /* _KERNEL_TTY_H */
