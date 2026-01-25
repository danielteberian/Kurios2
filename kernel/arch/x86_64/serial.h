/* serial.h - Serial port (UART) driver for x86_64 */
#ifndef _ARCH_SERIAL_H
#define _ARCH_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Serial port base addresses */
#define SERIAL_COM1     0x3F8
#define SERIAL_COM2     0x2F8
#define SERIAL_COM3     0x3E8
#define SERIAL_COM4     0x2E8

/* Default debug port */
#define SERIAL_DEBUG_PORT   SERIAL_COM1

/* Serial port register offsets */
#define SERIAL_DATA         0   /* Data register (R/W) */
#define SERIAL_IER          1   /* Interrupt Enable Register */
#define SERIAL_FCR          2   /* FIFO Control Register (W) */
#define SERIAL_IIR          2   /* Interrupt ID Register (R) */
#define SERIAL_LCR          3   /* Line Control Register */
#define SERIAL_MCR          4   /* Modem Control Register */
#define SERIAL_LSR          5   /* Line Status Register */
#define SERIAL_MSR          6   /* Modem Status Register */
#define SERIAL_SCRATCH      7   /* Scratch Register */

/* When DLAB=1, registers 0 and 1 become divisor latch */
#define SERIAL_DLL          0   /* Divisor Latch Low (DLAB=1) */
#define SERIAL_DLH          1   /* Divisor Latch High (DLAB=1) */

/* Line Control Register bits */
#define LCR_DLAB            0x80    /* Divisor Latch Access Bit */
#define LCR_BREAK           0x40    /* Break Enable */
#define LCR_PARITY_MASK     0x38    /* Parity bits */
#define LCR_STOP_2          0x04    /* 2 stop bits (1 if clear) */
#define LCR_WORD_8          0x03    /* 8 data bits */
#define LCR_WORD_7          0x02    /* 7 data bits */
#define LCR_WORD_6          0x01    /* 6 data bits */
#define LCR_WORD_5          0x00    /* 5 data bits */

/* Line Status Register bits */
#define LSR_DR              0x01    /* Data Ready */
#define LSR_OE              0x02    /* Overrun Error */
#define LSR_PE              0x04    /* Parity Error */
#define LSR_FE              0x08    /* Framing Error */
#define LSR_BI              0x10    /* Break Interrupt */
#define LSR_THRE            0x20    /* Transmitter Holding Register Empty */
#define LSR_TEMT            0x40    /* Transmitter Empty */
#define LSR_ERR_FIFO        0x80    /* Error in Receive FIFO */

/* FIFO Control Register bits */
#define FCR_ENABLE          0x01    /* Enable FIFOs */
#define FCR_CLEAR_RX        0x02    /* Clear Receive FIFO */
#define FCR_CLEAR_TX        0x04    /* Clear Transmit FIFO */
#define FCR_DMA_MODE        0x08    /* DMA Mode Select */
#define FCR_TRIGGER_1       0x00    /* Trigger at 1 byte */
#define FCR_TRIGGER_4       0x40    /* Trigger at 4 bytes */
#define FCR_TRIGGER_8       0x80    /* Trigger at 8 bytes */
#define FCR_TRIGGER_14      0xC0    /* Trigger at 14 bytes */

/* Modem Control Register bits */
#define MCR_DTR             0x01    /* Data Terminal Ready */
#define MCR_RTS             0x02    /* Request to Send */
#define MCR_OUT1            0x04    /* Output 1 */
#define MCR_OUT2            0x08    /* Output 2 (enables IRQ) */
#define MCR_LOOP            0x10    /* Loopback Mode */

/* Common baud rate divisors (115200 / baud) */
#define BAUD_115200         1
#define BAUD_57600          2
#define BAUD_38400          3
#define BAUD_19200          6
#define BAUD_9600           12
#define BAUD_4800           24
#define BAUD_2400           48
#define BAUD_1200           96

/* Function declarations */
void serial_init(uint16_t port, uint16_t baud_divisor);
void serial_init_default(void);
bool serial_is_transmit_ready(uint16_t port);
bool serial_is_receive_ready(uint16_t port);
void serial_putc(uint16_t port, char c);
char serial_getc(uint16_t port);
void serial_puts(uint16_t port, const char *str);
void serial_write(uint16_t port, const char *data, size_t len);

/* Debug port convenience functions */
void debug_putc(char c);
void debug_puts(const char *str);
void debug_write(const char *data, size_t len);

#endif /* _ARCH_SERIAL_H */
