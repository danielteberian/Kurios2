/* vga.c - VGA Text Mode Driver */

#include "vga.h"
#include "../mm/vmm.h"
#include "../debug/debug.h"

/* VGA text mode dimensions */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA buffer physical address */
#define VGA_PHYS_ADDR 0xB8000

/* Virtual address for VGA buffer - use address in kernel dynamic range */
#define VGA_VIRT_ADDR 0xFFFFFFFF8F000000UL

/* VGA buffer pointer */
static volatile uint16_t *vga_buffer = NULL;

/* Current cursor position */
static uint8_t cursor_x = 0;
static uint8_t cursor_y = 0;

/* Current color attribute */
static uint8_t current_color = 0x07;  /* Light gray on black */

/*
 * Make VGA entry (character + color)
 */
static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

/*
 * Scroll the screen up by one line
 */
static void vga_scroll(void) {
    /* Move all lines up */
    for (int y = 0; y < VGA_HEIGHT - 1; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    /* Clear the last line */
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', current_color);
    }
}

/*
 * Initialize VGA text mode
 */
void vga_init(void) {
    INFO("Initializing VGA text mode...");

    /* Map VGA buffer to virtual address */
    int ret = vmm_map_page(VGA_VIRT_ADDR, VGA_PHYS_ADDR, PTE_PRESENT | PTE_WRITABLE);
    if (ret != 0) {
        ERROR("Failed to map VGA buffer");
        return;
    }

    vga_buffer = (volatile uint16_t *)VGA_VIRT_ADDR;

    /* Clear screen and reset cursor */
    vga_clear();

    INFO("VGA initialized: virt 0x%llx -> phys 0x%llx", (uint64_t)VGA_VIRT_ADDR, (uint64_t)VGA_PHYS_ADDR);
}

/*
 * Clear the screen
 */
void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = vga_entry(' ', current_color);
    }
    cursor_x = 0;
    cursor_y = 0;
}

/*
 * Put a character at current cursor position
 */
void vga_putc(char c) {
    if (vga_buffer == NULL) return;

    switch (c) {
        case '\n':
            cursor_x = 0;
            cursor_y++;
            break;

        case '\r':
            cursor_x = 0;
            break;

        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(' ', current_color);
            }
            break;

        case '\t':
            cursor_x = (cursor_x + 8) & ~7;
            break;

        default:
            vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = vga_entry(c, current_color);
            cursor_x++;
            break;
    }

    /* Handle line wrap */
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    /* Handle scroll */
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll();
        cursor_y = VGA_HEIGHT - 1;
    }
}

/*
 * Put a string
 */
void vga_puts(const char *s) {
    while (*s) {
        vga_putc(*s++);
    }
}

/*
 * Set text color
 */
void vga_set_color(uint8_t fg, uint8_t bg) {
    current_color = fg | (bg << 4);
}
