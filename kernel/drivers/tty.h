/* tty.h - Terminal device with Line Discipline */
#ifndef _KERNEL_TTY_H
#define _KERNEL_TTY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "termios.h"
#include "../process/process.h"

/* TTY buffer sizes */
#define TTY_INPUT_SIZE      256     /* Raw input buffer */
#define TTY_CANON_SIZE      4096    /* Canonical line buffer */
#define TTY_OUTPUT_SIZE     256     /* Output buffer (unused for now) */

/*
 * TTY structure - represents a terminal device
 */
typedef struct tty {
    /* Input buffers */
    char input_buf[TTY_INPUT_SIZE];     /* Raw input (from keyboard IRQ) */
    volatile uint32_t input_head;       /* Write position */
    volatile uint32_t input_tail;       /* Read position */
    volatile uint32_t input_count;      /* Characters in raw buffer */

    /* Canonical mode line buffer */
    char canon_buf[TTY_CANON_SIZE];     /* Line being edited */
    uint32_t canon_len;                 /* Current line length */
    uint32_t canon_column;              /* Cursor column position */
    bool canon_ready;                   /* Line is ready (NL received) */

    /* Terminal settings */
    termios_t termios;                  /* Terminal I/O settings */
    winsize_t winsize;                  /* Window size */

    /* Foreground process group */
    pid_t fg_pgrp;                      /* Foreground process group ID */
    pid_t session;                      /* Controlling session ID */

    /* State flags */
    bool stopped;                       /* Output stopped (^S) */
    bool literal_next;                  /* Next char is literal (^V) */
} tty_t;

/*
 * Initialize TTY subsystem and create /dev/console
 */
void tty_init(void);

/*
 * Get the console TTY
 */
tty_t *tty_get_console(void);

/*
 * Write to TTY (called from VFS)
 */
int64_t tty_write(const void *buf, size_t size);

/*
 * Read from TTY (called from VFS)
 * In canonical mode, waits for complete line
 * In non-canonical mode, uses VMIN/VTIME settings
 */
int64_t tty_read(void *buf, size_t size);

/*
 * Add character to TTY input buffer (called from keyboard IRQ)
 * This processes the character through line discipline
 */
void tty_input_char(char c);

/*
 * TTY ioctl operations
 *
 * @param request  ioctl command (TCGETS, TCSETS, etc.)
 * @param arg      Pointer to argument structure
 * @return 0 on success, negative error on failure
 */
int tty_ioctl(unsigned long request, void *arg);

/*
 * Set foreground process group
 *
 * @param pgrp  Process group ID to set as foreground
 * @return 0 on success, negative error on failure
 */
int tty_set_fg_pgrp(pid_t pgrp);

/*
 * Get foreground process group
 */
pid_t tty_get_fg_pgrp(void);

/*
 * Set controlling terminal for a process
 *
 * @param proc  Process to set controlling terminal for
 * @return 0 on success, negative error on failure
 */
int tty_set_controlling(process_t *proc);

/*
 * Send signal to foreground process group
 *
 * @param signum  Signal number to send
 */
void tty_signal_fg(int signum);

/*
 * Flush TTY buffers
 *
 * @param queue  0 = input, 1 = output, 2 = both
 */
void tty_flush(int queue);

/*
 * Check if TTY is a terminal (for isatty())
 */
bool tty_is_tty(void);

/*
 * Line discipline functions for PTY reuse
 */

/*
 * Initialize termios settings to defaults
 *
 * @param t  termios structure to initialize
 */
void tty_init_termios(termios_t *t);

/*
 * Process input character through line discipline
 * Used by PTY master write path
 *
 * @param tty        TTY state (termios, canon_buf, etc.)
 * @param c          Input character
 * @param output_cb  Callback to send character to output (e.g., VGA, PTY m2s buffer)
 * @param echo_cb    Callback for echoing (same as output for console, s2m for PTY)
 * @param signal_cb  Callback to send signal to fg_pgrp
 * @param ctx        Context pointer passed to callbacks
 */
typedef void (*tty_output_cb_t)(void *ctx, char c);
typedef void (*tty_signal_cb_t)(void *ctx, int signum);

void tty_ldisc_input(tty_t *tty, char c,
                     tty_output_cb_t output_cb,
                     tty_output_cb_t echo_cb,
                     tty_signal_cb_t signal_cb,
                     void *ctx);

/*
 * Handle common TTY ioctls (TCGETS, TCSETS, etc.)
 * Returns 0 on success, negative error, or 1 if ioctl not handled
 *
 * @param tty      TTY state
 * @param request  ioctl command
 * @param arg      Argument pointer
 */
int tty_ioctl_common(tty_t *tty, unsigned long request, void *arg);

/*
 * Get random bytes (for getrandom syscall)
 * Uses the same PRNG as /dev/urandom
 *
 * @param buf   Buffer to fill with random bytes
 * @param size  Number of bytes to generate
 */
void get_random_bytes(void *buf, size_t size);

#endif /* _KERNEL_TTY_H */
