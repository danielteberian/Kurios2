/* io.h - x86_64 I/O port access */
#ifndef _ARCH_IO_H
#define _ARCH_IO_H

#include <stdint.h>

/* Read a byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Write a byte to an I/O port */
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Read a word (16-bit) from an I/O port */
static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Write a word (16-bit) to an I/O port */
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* Read a dword (32-bit) from an I/O port */
static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Write a dword (32-bit) to an I/O port */
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

/* I/O delay (for legacy devices that need time between accesses) */
static inline void io_wait(void) {
    /* Port 0x80 is used for POST codes, safe to write garbage */
    outb(0x80, 0);
}

/* Read a string of bytes from an I/O port */
static inline void insb(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile("rep insb"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/* Write a string of bytes to an I/O port */
static inline void outsb(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile("rep outsb"
                     : "+S"(addr), "+c"(count)
                     : "d"(port));
}

/* Read a string of words from an I/O port */
static inline void insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile("rep insw"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/* Write a string of words to an I/O port */
static inline void outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile("rep outsw"
                     : "+S"(addr), "+c"(count)
                     : "d"(port));
}

/* Read a string of dwords from an I/O port */
static inline void insl(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile("rep insl"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/* Write a string of dwords to an I/O port */
static inline void outsl(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile("rep outsl"
                     : "+S"(addr), "+c"(count)
                     : "d"(port));
}

#endif /* _ARCH_IO_H */
