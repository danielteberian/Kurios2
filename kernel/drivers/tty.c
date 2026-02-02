/* tty.c - Terminal device with Line Discipline */

#include "tty.h"
#include "vga.h"
#include "../fs/vfs.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "../signal/signal.h"
#include "../arch/x86_64/serial.h"
#include "../process/process.h"
#include "../process/pgrp.h"

/* Serial port for console output (COM1) */
#define CONSOLE_SERIAL_PORT 0x3F8

/* Console TTY instance */
static tty_t console_tty;

/* Forward declarations */
static void tty_process_input(tty_t *tty, char c);
static void tty_output_char(char c);

/* TTY node operations */
static ssize_t tty_node_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset);
static ssize_t tty_node_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset);
static int tty_node_ioctl(vfs_node_t *node, unsigned long request, void *arg);

static node_ops_t tty_ops = {
    .read = tty_node_read,
    .write = tty_node_write,
    .ioctl = tty_node_ioctl,
};

/* /dev/null - discards writes, returns EOF on read */
static ssize_t null_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    (void)node; (void)buf; (void)size; (void)offset;
    return 0;  /* EOF */
}

static ssize_t null_write(vfs_node_t *node, const void *buf, size_t size, uint64_t offset) {
    (void)node; (void)buf; (void)offset;
    return (ssize_t)size;  /* Accept all data */
}

static node_ops_t null_ops = {
    .read = null_read,
    .write = null_write,
};

/* /dev/zero - returns zeros on read, discards writes */
static ssize_t zero_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    (void)node; (void)offset;
    memset(buf, 0, size);
    return (ssize_t)size;
}

static node_ops_t zero_ops = {
    .read = zero_read,
    .write = null_write,  /* Same as /dev/null */
};

/* /dev/urandom - returns random bytes */
static uint64_t rng_state = 0;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t xorshift64(void) {
    if (rng_state == 0) {
        rng_state = rdtsc() ^ 0x5DEECE66DULL;
    }
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static ssize_t urandom_read(vfs_node_t *node, void *buf, size_t size, uint64_t offset) {
    (void)node; (void)offset;
    uint8_t *dst = (uint8_t *)buf;
    size_t i = 0;

    while (i < size) {
        uint64_t r = xorshift64();
        size_t chunk = (size - i < 8) ? (size - i) : 8;
        for (size_t j = 0; j < chunk; j++) {
            dst[i++] = (uint8_t)(r >> (j * 8));
        }
    }
    return (ssize_t)size;
}

/*
 * Get random bytes (for getrandom syscall)
 */
void get_random_bytes(void *buf, size_t size) {
    uint8_t *dst = (uint8_t *)buf;
    size_t i = 0;

    while (i < size) {
        uint64_t r = xorshift64();
        size_t chunk = (size - i < 8) ? (size - i) : 8;
        for (size_t j = 0; j < chunk; j++) {
            dst[i++] = (uint8_t)(r >> (j * 8));
        }
    }
}

static node_ops_t urandom_ops = {
    .read = urandom_read,
    .write = null_write,  /* Discard writes */
};

/*
 * Initialize default termios settings
 */
void tty_init_termios(termios_t *t) {
    t->c_iflag = TERMIOS_DEFAULT_IFLAG;
    t->c_oflag = TERMIOS_DEFAULT_OFLAG;
    t->c_cflag = TERMIOS_DEFAULT_CFLAG;
    t->c_lflag = TERMIOS_DEFAULT_LFLAG;
    t->c_line = 0;

    /* Set default control characters */
    memset(t->c_cc, 0, NCCS);
    t->c_cc[VINTR] = CINTR;
    t->c_cc[VQUIT] = CQUIT;
    t->c_cc[VERASE] = CERASE;
    t->c_cc[VKILL] = CKILL;
    t->c_cc[VEOF] = CEOF;
    t->c_cc[VTIME] = 0;
    t->c_cc[VMIN] = 1;
    t->c_cc[VSTART] = CSTART;
    t->c_cc[VSTOP] = CSTOP;
    t->c_cc[VSUSP] = CSUSP;
    t->c_cc[VEOL] = 0;
    t->c_cc[VREPRINT] = CREPRINT;
    t->c_cc[VDISCARD] = CDISCARD;
    t->c_cc[VWERASE] = CWERASE;
    t->c_cc[VLNEXT] = CLNEXT;
    t->c_cc[VEOL2] = 0;

    t->c_ispeed = 0;
    t->c_ospeed = 0;
}

/*
 * Get the console TTY
 */
tty_t *tty_get_console(void) {
    return &console_tty;
}

/*
 * Check if TTY is a terminal
 */
bool tty_is_tty(void) {
    return true;
}

/*
 * Output a single character to display (VGA and serial)
 */
static void tty_output_char(char c) {
    tty_t *tty = &console_tty;

    if (tty->stopped) {
        return;  /* Output stopped by ^S */
    }

    /* Output processing (c_oflag) */
    if (tty->termios.c_oflag & OPOST) {
        if ((tty->termios.c_oflag & ONLCR) && c == '\n') {
            /* Translate NL to CR-NL */
            vga_putc('\r');
            vga_putc('\n');
            serial_putc(CONSOLE_SERIAL_PORT, '\r');
            serial_putc(CONSOLE_SERIAL_PORT, '\n');
            return;
        }
    }

    vga_putc(c);
    serial_putc(CONSOLE_SERIAL_PORT, c);  /* Also output to serial */
}

/*
 * Send signal to foreground process group
 */
void tty_signal_fg(int signum) {
    tty_t *tty = &console_tty;

    if (tty->fg_pgrp == 0) {
        DEBUG("tty_signal_fg: no foreground process group");
        return;
    }

    DEBUG("TTY sending signal %d to pgrp %u", signum, tty->fg_pgrp);

    /* Send signal to all processes in the foreground process group */
    /* For now, just send to the process group leader */
    signal_send(tty->fg_pgrp, signum);
}

/*
 * Console-specific signal callback for line discipline
 */
static void console_signal_cb(void *ctx, int signum) {
    (void)ctx;
    tty_signal_fg(signum);
}

/*
 * Console-specific output callback
 */
static void console_output_cb(void *ctx, char c) {
    (void)ctx;
    tty_output_char(c);
}

/*
 * Line discipline context for internal helpers
 * Used to pass callbacks through static helper functions
 */
typedef struct ldisc_ctx {
    tty_t *tty;
    tty_output_cb_t output_cb;
    tty_output_cb_t echo_cb;
    tty_signal_cb_t signal_cb;
    void *user_ctx;
} ldisc_ctx_t;

/* Forward declarations for callback-based helpers */
static void ldisc_echo(ldisc_ctx_t *ctx, char c);
static void ldisc_echo_ctrl(ldisc_ctx_t *ctx, char c);
static void ldisc_erase_char(ldisc_ctx_t *ctx);
static void ldisc_kill_line(ldisc_ctx_t *ctx);
static void ldisc_word_erase(ldisc_ctx_t *ctx);
static void ldisc_flush_input(tty_t *tty);

/*
 * Echo a character using callback
 */
static void ldisc_echo(ldisc_ctx_t *ctx, char c) {
    tty_t *tty = ctx->tty;

    if (!(tty->termios.c_lflag & ECHO)) {
        return;  /* Echo disabled */
    }

    /* Control character echoing */
    if (c < 32 && c != '\n' && c != '\t' && c != '\r') {
        if (tty->termios.c_lflag & ECHOCTL) {
            ldisc_echo_ctrl(ctx, c);
        }
        return;
    }

    ctx->echo_cb(ctx->user_ctx, c);
}

/*
 * Echo a control character as ^X using callback
 */
static void ldisc_echo_ctrl(ldisc_ctx_t *ctx, char c) {
    ctx->echo_cb(ctx->user_ctx, '^');
    ctx->echo_cb(ctx->user_ctx, c + '@');
}

/*
 * Erase one character (backspace) using callback
 */
static void ldisc_erase_char(ldisc_ctx_t *ctx) {
    tty_t *tty = ctx->tty;

    if (tty->canon_len == 0) {
        return;
    }

    /* Get the character being erased */
    char erased = tty->canon_buf[tty->canon_len - 1];
    tty->canon_len--;

    /* Visual echo of erase */
    if (tty->termios.c_lflag & ECHO) {
        if (tty->termios.c_lflag & ECHOE) {
            /* Echo backspace-space-backspace */
            ctx->echo_cb(ctx->user_ctx, '\b');
            ctx->echo_cb(ctx->user_ctx, ' ');
            ctx->echo_cb(ctx->user_ctx, '\b');

            /* If erased char was a control char displayed as ^X, erase that too */
            if (erased < 32 && erased != '\t' && (tty->termios.c_lflag & ECHOCTL)) {
                ctx->echo_cb(ctx->user_ctx, '\b');
                ctx->echo_cb(ctx->user_ctx, ' ');
                ctx->echo_cb(ctx->user_ctx, '\b');
            }
        }
    }
}

/*
 * Kill entire line (Ctrl+U) using callback
 */
static void ldisc_kill_line(ldisc_ctx_t *ctx) {
    tty_t *tty = ctx->tty;

    if (tty->termios.c_lflag & ECHO) {
        if (tty->termios.c_lflag & ECHOKE) {
            /* Visual erase entire line */
            while (tty->canon_len > 0) {
                ldisc_erase_char(ctx);
            }
        } else if (tty->termios.c_lflag & ECHOK) {
            /* Just echo newline */
            ctx->echo_cb(ctx->user_ctx, '\n');
        }
    }
    tty->canon_len = 0;
}

/*
 * Word erase (Ctrl+W) using callback
 */
static void ldisc_word_erase(ldisc_ctx_t *ctx) {
    tty_t *tty = ctx->tty;

    /* Skip trailing whitespace */
    while (tty->canon_len > 0 && tty->canon_buf[tty->canon_len - 1] == ' ') {
        ldisc_erase_char(ctx);
    }
    /* Erase word */
    while (tty->canon_len > 0 && tty->canon_buf[tty->canon_len - 1] != ' ') {
        ldisc_erase_char(ctx);
    }
}

/*
 * Flush input buffers
 */
static void ldisc_flush_input(tty_t *tty) {
    tty->input_head = 0;
    tty->input_tail = 0;
    tty->input_count = 0;
    tty->canon_len = 0;
    tty->canon_ready = false;
    tty->literal_next = false;
}

/*
 * Process an input character through line discipline with callbacks
 * This is the core line discipline function, reusable by PTY
 */
void tty_ldisc_input(tty_t *tty, char c,
                     tty_output_cb_t output_cb,
                     tty_output_cb_t echo_cb,
                     tty_signal_cb_t signal_cb,
                     void *ctx) {
    /* Set up context for helper functions */
    ldisc_ctx_t lctx = {
        .tty = tty,
        .output_cb = output_cb,
        .echo_cb = echo_cb,
        .signal_cb = signal_cb,
        .user_ctx = ctx
    };

    /* Handle literal next (Ctrl+V) */
    if (tty->literal_next) {
        tty->literal_next = false;
        goto add_char;
    }

    /* Input processing (c_iflag) */
    if (tty->termios.c_iflag & ISTRIP) {
        c &= 0x7f;  /* Strip 8th bit */
    }

    if (tty->termios.c_iflag & ICRNL && c == '\r') {
        c = '\n';  /* Translate CR to NL */
    } else if (tty->termios.c_iflag & IGNCR && c == '\r') {
        return;  /* Ignore CR */
    } else if (tty->termios.c_iflag & INLCR && c == '\n') {
        c = '\r';  /* Translate NL to CR */
    }

    /* Signal characters (ISIG mode) */
    if (tty->termios.c_lflag & ISIG) {
        if (c == tty->termios.c_cc[VINTR]) {
            /* Interrupt (Ctrl+C) -> SIGINT */
            if (tty->termios.c_lflag & ECHO) {
                ldisc_echo_ctrl(&lctx, c);
                echo_cb(ctx, '\n');
            }
            if (!(tty->termios.c_lflag & NOFLSH)) {
                ldisc_flush_input(tty);
            }
            if (signal_cb) signal_cb(ctx, SIGINT);
            return;
        }

        if (c == tty->termios.c_cc[VQUIT]) {
            /* Quit (Ctrl+\) -> SIGQUIT */
            if (tty->termios.c_lflag & ECHO) {
                ldisc_echo_ctrl(&lctx, c);
                echo_cb(ctx, '\n');
            }
            if (!(tty->termios.c_lflag & NOFLSH)) {
                ldisc_flush_input(tty);
            }
            if (signal_cb) signal_cb(ctx, SIGQUIT);
            return;
        }

        if (c == tty->termios.c_cc[VSUSP]) {
            /* Suspend (Ctrl+Z) -> SIGTSTP */
            if (tty->termios.c_lflag & ECHO) {
                ldisc_echo_ctrl(&lctx, c);
                echo_cb(ctx, '\n');
            }
            if (!(tty->termios.c_lflag & NOFLSH)) {
                ldisc_flush_input(tty);
            }
            if (signal_cb) signal_cb(ctx, SIGTSTP);
            return;
        }
    }

    /* Flow control (IXON) */
    if (tty->termios.c_iflag & IXON) {
        if (c == tty->termios.c_cc[VSTOP]) {
            /* Stop output (Ctrl+S) */
            tty->stopped = true;
            return;
        }
        if (c == tty->termios.c_cc[VSTART] ||
            ((tty->termios.c_iflag & IXANY) && tty->stopped)) {
            /* Start output (Ctrl+Q or any char with IXANY) */
            tty->stopped = false;
            if (c == tty->termios.c_cc[VSTART]) {
                return;
            }
        }
    }

    /* Canonical mode processing */
    if (tty->termios.c_lflag & ICANON) {
        /* Literal next (Ctrl+V) */
        if (c == tty->termios.c_cc[VLNEXT]) {
            tty->literal_next = true;
            if (tty->termios.c_lflag & ECHO) {
                echo_cb(ctx, '^');
                echo_cb(ctx, '\b');
            }
            return;
        }

        /* Erase character (backspace/delete) */
        if (c == tty->termios.c_cc[VERASE] || c == '\b' || c == 0x7f) {
            ldisc_erase_char(&lctx);
            return;
        }

        /* Kill line (Ctrl+U) */
        if (c == tty->termios.c_cc[VKILL]) {
            ldisc_kill_line(&lctx);
            return;
        }

        /* Word erase (Ctrl+W) */
        if (c == tty->termios.c_cc[VWERASE]) {
            ldisc_word_erase(&lctx);
            return;
        }

        /* EOF (Ctrl+D) */
        if (c == tty->termios.c_cc[VEOF]) {
            tty->canon_ready = true;  /* Signal EOF */
            return;
        }

        /* Reprint line (Ctrl+R) */
        if (c == tty->termios.c_cc[VREPRINT]) {
            if (tty->termios.c_lflag & ECHO) {
                ldisc_echo_ctrl(&lctx, c);
                echo_cb(ctx, '\n');
                for (uint32_t i = 0; i < tty->canon_len; i++) {
                    ldisc_echo(&lctx, tty->canon_buf[i]);
                }
            }
            return;
        }

        /* End of line - make line available to reader */
        if (c == '\n' || c == tty->termios.c_cc[VEOL] ||
            (tty->termios.c_cc[VEOL2] && c == tty->termios.c_cc[VEOL2])) {
            if (tty->canon_len < TTY_CANON_SIZE - 1) {
                tty->canon_buf[tty->canon_len++] = c;
            }
            ldisc_echo(&lctx, c);
            tty->canon_ready = true;
            return;
        }

        /* Regular character in canonical mode */
        if (tty->canon_len < TTY_CANON_SIZE - 1) {
            tty->canon_buf[tty->canon_len++] = c;
            ldisc_echo(&lctx, c);
        }
        return;
    }

add_char:
    /* Non-canonical mode: add to raw input buffer */
    if (tty->input_count < TTY_INPUT_SIZE) {
        tty->input_buf[tty->input_head] = c;
        tty->input_head = (tty->input_head + 1) % TTY_INPUT_SIZE;
        tty->input_count++;
        ldisc_echo(&lctx, c);
    }
}

/*
 * Process an input character through line discipline (console wrapper)
 */
static void tty_process_input(tty_t *tty, char c) {
    tty_ldisc_input(tty, c, console_output_cb, console_output_cb,
                    console_signal_cb, NULL);
}

/*
 * Check if current process can read from TTY (background access control)
 * Returns 0 if allowed, negative error otherwise
 */
static int tty_check_read_access(tty_t *tty) {
    process_t *proc = process_current();
    if (!proc) {
        return 0;  /* Kernel thread, allow access */
    }

    /* Check if process is in foreground group */
    if (tty->fg_pgrp != 0 && proc->pgrp != tty->fg_pgrp) {
        /* Background process trying to read */

        /* Check if SIGTTIN is ignored or blocked */
        if (proc->signals) {
            sigaction_t *action = &proc->signals->actions[SIGTTIN];
            bool ignored = (action->sa_handler == SIG_IGN);
            bool blocked = sigismember(&proc->signals->blocked, SIGTTIN);

            if (ignored) {
                /* SIGTTIN ignored: return EIO */
                return -5;  /* EIO */
            }

            if (!blocked) {
                /* Send SIGTTIN to process group */
                DEBUG("TTY: Background read attempt by PID %u (pgrp %u), sending SIGTTIN",
                      proc->pid, proc->pgrp);
                pgrp_send_signal(proc->pgrp, SIGTTIN);
                return -4;  /* EINTR */
            }
        }

        /* Signal blocked: return EIO */
        return -5;  /* EIO */
    }

    return 0;  /* Foreground process or no foreground group set */
}

/*
 * Check if current process can write to TTY (background access control)
 * Returns 0 if allowed, negative error otherwise
 */
static int tty_check_write_access(tty_t *tty) {
    process_t *proc = process_current();
    if (!proc) {
        return 0;  /* Kernel thread, allow access */
    }

    /* Check TOSTOP flag */
    if (!(tty->termios.c_lflag & TOSTOP)) {
        return 0;  /* TOSTOP not set, allow background writes */
    }

    /* Check if process is in foreground group */
    if (tty->fg_pgrp != 0 && proc->pgrp != tty->fg_pgrp) {
        /* Background process trying to write with TOSTOP set */

        /* Check if SIGTTOU is ignored or blocked */
        if (proc->signals) {
            sigaction_t *action = &proc->signals->actions[SIGTTOU];
            bool ignored = (action->sa_handler == SIG_IGN);
            bool blocked = sigismember(&proc->signals->blocked, SIGTTOU);

            if (ignored) {
                /* SIGTTOU ignored: allow write */
                return 0;
            }

            if (!blocked) {
                /* Send SIGTTOU to process group */
                DEBUG("TTY: Background write attempt by PID %u (pgrp %u), sending SIGTTOU",
                      proc->pid, proc->pgrp);
                pgrp_send_signal(proc->pgrp, SIGTTOU);
                return -4;  /* EINTR */
            }
        }

        /* Signal blocked: return EIO */
        return -5;  /* EIO */
    }

    return 0;  /* Foreground process or no foreground group set */
}

/*
 * Write to TTY (VGA output with processing)
 */
int64_t tty_write(const void *buf, size_t size) {
    tty_t *tty = &console_tty;

    /* Check background write access */
    int access = tty_check_write_access(tty);
    if (access < 0) {
        return access;
    }

    const char *str = (const char *)buf;
    for (size_t i = 0; i < size; i++) {
        tty_output_char(str[i]);
    }
    return (int64_t)size;
}

/*
 * Read from TTY
 */
int64_t tty_read(void *buf, size_t size) {
    tty_t *tty = &console_tty;
    char *dst = (char *)buf;
    size_t bytes_read = 0;

    /* Check background read access */
    int access = tty_check_read_access(tty);
    if (access < 0) {
        return access;
    }

    if (tty->termios.c_lflag & ICANON) {
        /* Canonical mode: wait for line */
        if (!tty->canon_ready) {
            return -11;  /* EAGAIN */
        }

        /* Copy line to user buffer */
        while (bytes_read < size && bytes_read < tty->canon_len) {
            dst[bytes_read] = tty->canon_buf[bytes_read];
            bytes_read++;
        }

        /* Remove copied data from buffer */
        if (bytes_read == tty->canon_len) {
            tty->canon_len = 0;
            tty->canon_ready = false;
        } else {
            /* Partial read - shift remaining data */
            memmove(tty->canon_buf, tty->canon_buf + bytes_read,
                    tty->canon_len - bytes_read);
            tty->canon_len -= bytes_read;
        }

        return (int64_t)bytes_read;
    } else {
        /* Non-canonical mode: use VMIN/VTIME */
        uint8_t vmin = tty->termios.c_cc[VMIN];
        /* VTIME unused for now */

        if (tty->input_count == 0) {
            if (vmin == 0) {
                return 0;  /* No data, no minimum required */
            }
            return -11;  /* EAGAIN */
        }

        /* Read available characters up to size */
        while (bytes_read < size && tty->input_count > 0) {
            dst[bytes_read++] = tty->input_buf[tty->input_tail];
            tty->input_tail = (tty->input_tail + 1) % TTY_INPUT_SIZE;
            tty->input_count--;
        }

        return (int64_t)bytes_read;
    }
}

/*
 * Add character to TTY input (called from keyboard IRQ)
 */
void tty_input_char(char c) {
    tty_process_input(&console_tty, c);
}

/*
 * Common TTY ioctl handler (reusable by PTY)
 * Returns 0 on success, negative error, or 1 if ioctl not handled
 */
int tty_ioctl_common(tty_t *tty, unsigned long request, void *arg) {
    switch (request) {
        case TCGETS:
            if (!arg) return -22;  /* EINVAL */
            memcpy(arg, &tty->termios, sizeof(termios_t));
            return 0;

        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            if (!arg) return -22;
            if (request == TCSETSF) {
                /* Flush input */
                tty->input_head = 0;
                tty->input_tail = 0;
                tty->input_count = 0;
                tty->canon_len = 0;
                tty->canon_ready = false;
                tty->literal_next = false;
            }
            memcpy(&tty->termios, arg, sizeof(termios_t));
            return 0;

        case TIOCGPGRP:
            if (!arg) return -22;
            *(pid_t *)arg = tty->fg_pgrp;
            return 0;

        case TIOCSPGRP:
            if (!arg) return -22;
            tty->fg_pgrp = *(pid_t *)arg;
            return 0;

        case TIOCGWINSZ:
            if (!arg) return -22;
            memcpy(arg, &tty->winsize, sizeof(winsize_t));
            return 0;

        case TIOCSWINSZ:
            if (!arg) return -22;
            memcpy(&tty->winsize, arg, sizeof(winsize_t));
            /* Could send SIGWINCH here */
            return 0;

        case TIOCSCTTY:
            /* Set controlling terminal - simplified */
            return 0;

        default:
            return 1;  /* Not handled */
    }
}

/*
 * TTY ioctl (console wrapper)
 */
int tty_ioctl(unsigned long request, void *arg) {
    int ret = tty_ioctl_common(&console_tty, request, arg);
    if (ret == 1) {
        DEBUG("tty_ioctl: unknown request 0x%lx", request);
        return -22;  /* EINVAL */
    }
    return ret;
}

/*
 * Set foreground process group
 */
int tty_set_fg_pgrp(pid_t pgrp) {
    console_tty.fg_pgrp = pgrp;
    return 0;
}

/*
 * Get foreground process group
 */
pid_t tty_get_fg_pgrp(void) {
    return console_tty.fg_pgrp;
}

/*
 * Set controlling terminal
 */
int tty_set_controlling(process_t *proc) {
    if (!proc) return -22;
    console_tty.session = proc->session_id;
    console_tty.fg_pgrp = proc->pgrp;
    return 0;
}

/*
 * Flush TTY buffers
 */
void tty_flush(int queue) {
    tty_t *tty = &console_tty;

    if (queue == 0 || queue == 2) {
        /* Flush input */
        tty->input_head = 0;
        tty->input_tail = 0;
        tty->input_count = 0;
        tty->canon_len = 0;
        tty->canon_ready = false;
        tty->literal_next = false;
    }

    if (queue == 1 || queue == 2) {
        /* Flush output (nothing to do currently) */
    }
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
 * VFS ioctl callback
 */
static int tty_node_ioctl(vfs_node_t *node, unsigned long request, void *arg) {
    (void)node;
    return tty_ioctl(request, arg);
}

/*
 * Initialize TTY and create /dev/console
 */
void tty_init(void) {
    INFO("Initializing TTY subsystem with line discipline...");

    /* Initialize console TTY */
    memset(&console_tty, 0, sizeof(tty_t));
    tty_init_termios(&console_tty.termios);

    /* Set default window size (80x25 VGA text mode) */
    console_tty.winsize.ws_row = 25;
    console_tty.winsize.ws_col = 80;
    console_tty.winsize.ws_xpixel = 0;
    console_tty.winsize.ws_ypixel = 0;

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
    console->rdev = MAKEDEV(TTYAUX_MAJOR, 1);  /* 5,1 */
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

    /* Create /dev/tty as alias to console */
    vfs_node_t *tty_node = vfs_node_alloc();
    if (tty_node) {
        strcpy(tty_node->name, "tty");
        tty_node->type = VFS_CHARDEV;
        tty_node->ops = &tty_ops;
        tty_node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
        tty_node->rdev = MAKEDEV(TTYAUX_MAJOR, 0);  /* 5,0 */
        tty_node->ref_count = 1;
        tty_node->parent = dev;
        tty_node->next = dev->children;
        if (dev->children) dev->children->prev = tty_node;
        dev->children = tty_node;
    }

    /* Create /dev/null */
    vfs_node_t *null_node = vfs_node_alloc();
    if (null_node) {
        strcpy(null_node->name, "null");
        null_node->type = VFS_CHARDEV;
        null_node->ops = &null_ops;
        null_node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
        null_node->rdev = MAKEDEV(MEM_MAJOR, 3);  /* 1,3 */
        null_node->ref_count = 1;
        null_node->parent = dev;
        null_node->next = dev->children;
        if (dev->children) dev->children->prev = null_node;
        dev->children = null_node;
    }

    /* Create /dev/zero */
    vfs_node_t *zero_node = vfs_node_alloc();
    if (zero_node) {
        strcpy(zero_node->name, "zero");
        zero_node->type = VFS_CHARDEV;
        zero_node->ops = &zero_ops;
        zero_node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
        zero_node->rdev = MAKEDEV(MEM_MAJOR, 5);  /* 1,5 */
        zero_node->ref_count = 1;
        zero_node->parent = dev;
        zero_node->next = dev->children;
        if (dev->children) dev->children->prev = zero_node;
        dev->children = zero_node;
    }

    /* Create /dev/urandom */
    vfs_node_t *urandom_node = vfs_node_alloc();
    if (urandom_node) {
        strcpy(urandom_node->name, "urandom");
        urandom_node->type = VFS_CHARDEV;
        urandom_node->ops = &urandom_ops;
        urandom_node->permissions = VFS_PERM_READ | VFS_PERM_WRITE;
        urandom_node->rdev = MAKEDEV(MEM_MAJOR, 9);  /* 1,9 */
        urandom_node->ref_count = 1;
        urandom_node->parent = dev;
        urandom_node->next = dev->children;
        if (dev->children) dev->children->prev = urandom_node;
        dev->children = urandom_node;
    }

    vfs_node_unref(dev);
    INFO("TTY initialized: /dev/console, /dev/tty, /dev/null, /dev/zero, /dev/urandom");
    INFO("Line discipline: canonical mode, echo enabled, signals enabled");
}
