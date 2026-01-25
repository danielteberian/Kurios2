/* serial.c - Serial port (UART) driver for x86_64 */

#include "serial.h"
#include "io.h"

/* Initialize a serial port */
void serial_init(uint16_t port, uint16_t baud_divisor) {
    /* Disable interrupts */
    outb(port + SERIAL_IER, 0x00);

    /* Enable DLAB to set baud rate divisor */
    outb(port + SERIAL_LCR, LCR_DLAB);

    /* Set divisor (low byte, then high byte) */
    outb(port + SERIAL_DLL, baud_divisor & 0xFF);
    outb(port + SERIAL_DLH, (baud_divisor >> 8) & 0xFF);

    /* 8 bits, no parity, one stop bit (8N1), disable DLAB */
    outb(port + SERIAL_LCR, LCR_WORD_8);

    /* Enable FIFO, clear them, 14-byte threshold */
    outb(port + SERIAL_FCR, FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX | FCR_TRIGGER_14);

    /* Enable IRQs, RTS/DSR set (loopback mode for testing, then normal) */
    outb(port + SERIAL_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    /* Test serial chip (send byte in loopback mode) */
    outb(port + SERIAL_MCR, MCR_DTR | MCR_RTS | MCR_OUT2 | MCR_LOOP);
    outb(port + SERIAL_DATA, 0xAE);

    /* Check if serial loopback works */
    if (inb(port + SERIAL_DATA) != 0xAE) {
        /* Serial port not working properly, but continue anyway */
    }

    /* Set to normal operation mode */
    outb(port + SERIAL_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
}

/* Initialize COM1 at 115200 baud (default debug port) */
void serial_init_default(void) {
    serial_init(SERIAL_DEBUG_PORT, BAUD_115200);
}

/* Check if transmit buffer is empty */
bool serial_is_transmit_ready(uint16_t port) {
    return (inb(port + SERIAL_LSR) & LSR_THRE) != 0;
}

/* Check if data is available to read */
bool serial_is_receive_ready(uint16_t port) {
    return (inb(port + SERIAL_LSR) & LSR_DR) != 0;
}

/* Write a single character (blocking) */
void serial_putc(uint16_t port, char c) {
    /* Wait for transmit buffer to be empty */
    while (!serial_is_transmit_ready(port)) {
        __asm__ volatile("pause");
    }
    outb(port + SERIAL_DATA, c);
}

/* Read a single character (blocking) */
char serial_getc(uint16_t port) {
    /* Wait for data to be available */
    while (!serial_is_receive_ready(port)) {
        __asm__ volatile("pause");
    }
    return inb(port + SERIAL_DATA);
}

/* Write a null-terminated string */
void serial_puts(uint16_t port, const char *str) {
    while (*str) {
        if (*str == '\n') {
            serial_putc(port, '\r');
        }
        serial_putc(port, *str++);
    }
}

/* Write a buffer of specified length */
void serial_write(uint16_t port, const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n') {
            serial_putc(port, '\r');
        }
        serial_putc(port, data[i]);
    }
}

/* Debug port convenience functions */
void debug_putc(char c) {
    if (c == '\n') {
        serial_putc(SERIAL_DEBUG_PORT, '\r');
    }
    serial_putc(SERIAL_DEBUG_PORT, c);
}

void debug_puts(const char *str) {
    serial_puts(SERIAL_DEBUG_PORT, str);
}

void debug_write(const char *data, size_t len) {
    serial_write(SERIAL_DEBUG_PORT, data, len);
}
