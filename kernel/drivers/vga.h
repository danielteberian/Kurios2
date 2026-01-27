/* vga.h - VGA Text Mode Driver */
#ifndef _DRIVERS_VGA_H
#define _DRIVERS_VGA_H

#include <stdint.h>

/* VGA colors */
#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x6
#define VGA_LIGHT_GRAY    0x7
#define VGA_DARK_GRAY     0x8
#define VGA_LIGHT_BLUE    0x9
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_LIGHT_MAGENTA 0xD
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

/* Initialize VGA text mode */
void vga_init(void);

/* Clear screen */
void vga_clear(void);

/* Put a character at current cursor position */
void vga_putc(char c);

/* Put a string */
void vga_puts(const char *s);

/* Set text color (foreground | (background << 4)) */
void vga_set_color(uint8_t fg, uint8_t bg);

#endif /* _DRIVERS_VGA_H */
