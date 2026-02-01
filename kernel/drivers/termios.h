/* termios.h - Terminal I/O Settings */
#ifndef _KERNEL_TERMIOS_H
#define _KERNEL_TERMIOS_H

#include <stdint.h>

/*
 * Control character indices (c_cc array)
 */
#define VINTR       0   /* Interrupt character (SIGINT) - Ctrl+C */
#define VQUIT       1   /* Quit character (SIGQUIT) - Ctrl+\ */
#define VERASE      2   /* Erase character - Backspace/Delete */
#define VKILL       3   /* Kill line character - Ctrl+U */
#define VEOF        4   /* End-of-file character - Ctrl+D */
#define VTIME       5   /* Timeout for non-canonical read */
#define VMIN        6   /* Minimum chars for non-canonical read */
#define VSWTC       7   /* Switch character (unused) */
#define VSTART      8   /* Start output character - Ctrl+Q */
#define VSTOP       9   /* Stop output character - Ctrl+S */
#define VSUSP       10  /* Suspend character (SIGTSTP) - Ctrl+Z */
#define VEOL        11  /* End-of-line character */
#define VREPRINT    12  /* Reprint line character - Ctrl+R */
#define VDISCARD    13  /* Discard character - Ctrl+O */
#define VWERASE     14  /* Word erase character - Ctrl+W */
#define VLNEXT      15  /* Literal next character - Ctrl+V */
#define VEOL2       16  /* Second end-of-line character */

#define NCCS        17  /* Number of control characters */

/*
 * Input modes (c_iflag)
 */
#define IGNBRK      0x00001     /* Ignore break condition */
#define BRKINT      0x00002     /* Signal SIGINT on break */
#define IGNPAR      0x00004     /* Ignore parity errors */
#define PARMRK      0x00008     /* Mark parity errors */
#define INPCK       0x00010     /* Enable input parity checking */
#define ISTRIP      0x00020     /* Strip 8th bit */
#define INLCR       0x00040     /* Translate NL to CR on input */
#define IGNCR       0x00080     /* Ignore CR on input */
#define ICRNL       0x00100     /* Translate CR to NL on input */
#define IUCLC       0x00200     /* Map uppercase to lowercase */
#define IXON        0x00400     /* Enable XON/XOFF flow control on output */
#define IXANY       0x00800     /* Any character will restart after stop */
#define IXOFF       0x01000     /* Enable XON/XOFF flow control on input */
#define IMAXBEL     0x02000     /* Ring bell when input queue is full */
#define IUTF8       0x04000     /* Input is UTF8 */

/*
 * Output modes (c_oflag)
 */
#define OPOST       0x00001     /* Post-process output */
#define OLCUC       0x00002     /* Map lowercase to uppercase */
#define ONLCR       0x00004     /* Translate NL to CR-NL on output */
#define OCRNL       0x00008     /* Translate CR to NL on output */
#define ONOCR       0x00010     /* Don't output CR at column 0 */
#define ONLRET      0x00020     /* NL performs CR function */
#define OFILL       0x00040     /* Use fill characters for delay */
#define OFDEL       0x00080     /* Fill character is DEL (vs NUL) */

/*
 * Control modes (c_cflag)
 */
#define CSIZE       0x00030     /* Character size mask */
#define   CS5       0x00000     /* 5 bits */
#define   CS6       0x00010     /* 6 bits */
#define   CS7       0x00020     /* 7 bits */
#define   CS8       0x00030     /* 8 bits */
#define CSTOPB      0x00040     /* 2 stop bits (vs 1) */
#define CREAD       0x00080     /* Enable receiver */
#define PARENB      0x00100     /* Enable parity */
#define PARODD      0x00200     /* Odd parity (vs even) */
#define HUPCL       0x00400     /* Hangup on last close */
#define CLOCAL      0x00800     /* Ignore modem control lines */

/*
 * Local modes (c_lflag)
 */
#define ISIG        0x00001     /* Enable signals (SIGINT, SIGQUIT, SIGSUSP) */
#define ICANON      0x00002     /* Canonical mode (line editing) */
#define XCASE       0x00004     /* Canonical upper/lower presentation */
#define ECHO        0x00008     /* Echo input characters */
#define ECHOE       0x00010     /* Echo ERASE as BS-SP-BS */
#define ECHOK       0x00020     /* Echo NL after KILL character */
#define ECHONL      0x00040     /* Echo NL even if ECHO is off */
#define NOFLSH      0x00080     /* Don't flush after interrupt */
#define TOSTOP      0x00100     /* Send SIGTTOU for background output */
#define ECHOCTL     0x00200     /* Echo control chars as ^X */
#define ECHOPRT     0x00400     /* Echo erased chars */
#define ECHOKE      0x00800     /* Visual erase for line kill */
#define FLUSHO      0x01000     /* Output being flushed */
#define PENDIN      0x02000     /* Retype pending input */
#define IEXTEN      0x04000     /* Extended input processing */

/*
 * ioctl commands
 */
#define TCGETS      0x5401      /* Get termios structure */
#define TCSETS      0x5402      /* Set termios structure immediately */
#define TCSETSW     0x5403      /* Set termios after draining output */
#define TCSETSF     0x5404      /* Set termios after flushing */
#define TIOCGPGRP   0x540F      /* Get foreground process group */
#define TIOCSPGRP   0x5410      /* Set foreground process group */
#define TIOCGWINSZ  0x5413      /* Get window size */
#define TIOCSWINSZ  0x5414      /* Set window size */
#define TIOCSCTTY   0x540E      /* Set controlling terminal */
#define TIOCGPTN    0x80045430  /* Get PTY number */
#define TIOCSPTLCK  0x40045431  /* Lock/unlock PTY slave */

/*
 * Window size structure
 */
typedef struct winsize {
    uint16_t ws_row;        /* Number of rows */
    uint16_t ws_col;        /* Number of columns */
    uint16_t ws_xpixel;     /* Width in pixels (unused) */
    uint16_t ws_ypixel;     /* Height in pixels (unused) */
} winsize_t;

/*
 * termios structure - terminal settings
 */
typedef struct termios {
    uint32_t c_iflag;       /* Input mode flags */
    uint32_t c_oflag;       /* Output mode flags */
    uint32_t c_cflag;       /* Control mode flags */
    uint32_t c_lflag;       /* Local mode flags */
    uint8_t c_line;         /* Line discipline */
    uint8_t c_cc[NCCS];     /* Control characters */
    uint32_t c_ispeed;      /* Input speed (unused) */
    uint32_t c_ospeed;      /* Output speed (unused) */
} termios_t;

/*
 * Default termios values (cooked mode with echo)
 */
#define TERMIOS_DEFAULT_IFLAG   (ICRNL | IXON)
#define TERMIOS_DEFAULT_OFLAG   (OPOST | ONLCR)
#define TERMIOS_DEFAULT_CFLAG   (CS8 | CREAD | CLOCAL)
#define TERMIOS_DEFAULT_LFLAG   (ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE)

/*
 * Control character defaults
 */
#define CTRL(x)     ((x) & 0x1f)    /* Ctrl+x */

#define CINTR       CTRL('C')   /* ^C */
#define CQUIT       CTRL('\\')  /* ^\ */
#define CERASE      0x7f        /* DEL (backspace) */
#define CKILL       CTRL('U')   /* ^U */
#define CEOF        CTRL('D')   /* ^D */
#define CSTART      CTRL('Q')   /* ^Q */
#define CSTOP       CTRL('S')   /* ^S */
#define CSUSP       CTRL('Z')   /* ^Z */
#define CREPRINT    CTRL('R')   /* ^R */
#define CDISCARD    CTRL('O')   /* ^O */
#define CWERASE     CTRL('W')   /* ^W */
#define CLNEXT      CTRL('V')   /* ^V */

#endif /* _KERNEL_TERMIOS_H */
