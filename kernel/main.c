/* main.c - Minimal kernel for testing bootloader */

#include "../boot/common/boot_info.h"

/* VGA text mode buffer */
static volatile uint16_t *vga_buffer = (uint16_t*)0xB8000;
static int cursor_x = 0;
static int cursor_y = 0;

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_COLOR  0x0F00  /* White on black */

static void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = VGA_COLOR | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
}

static void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = VGA_COLOR | c;
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }

    if (cursor_y >= VGA_HEIGHT) {
        /* Scroll up */
        for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
            vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
        }
        for (int i = 0; i < VGA_WIDTH; i++) {
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = VGA_COLOR | ' ';
        }
        cursor_y = VGA_HEIGHT - 1;
    }
}

static void vga_puts(const char *str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

static void vga_put_hex(uint64_t value) {
    const char *hex = "0123456789ABCDEF";
    char buf[17];
    buf[16] = 0;

    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    vga_puts(buf);
}

static void fb_clear(FramebufferInfo *fb, uint32_t color) {
    uint32_t *pixels = (uint32_t*)(uintptr_t)fb->address;
    uint32_t total = (fb->pitch / 4) * fb->height;

    for (uint32_t i = 0; i < total; i++) {
        pixels[i] = color;
    }
}

static void fb_draw_test(FramebufferInfo *fb) {
    uint32_t *pixels = (uint32_t*)(uintptr_t)fb->address;
    uint32_t pitch = fb->pitch / 4;

    /* Draw colorful pattern */
    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            uint8_t r = (x * 255) / fb->width;
            uint8_t g = (y * 255) / fb->height;
            uint8_t b = 128;
            pixels[y * pitch + x] = (r << 16) | (g << 8) | b;
        }
    }

    /* Draw white rectangle in center */
    uint32_t cx = fb->width / 2;
    uint32_t cy = fb->height / 2;
    for (uint32_t y = cy - 50; y < cy + 50; y++) {
        for (uint32_t x = cx - 100; x < cx + 100; x++) {
            pixels[y * pitch + x] = 0xFFFFFF;
        }
    }
}

/* Kernel main - called by bootloader */
void kernel_main(BootInfo *boot_info) {
    /* Verify boot info magic */
    if (boot_info == 0 || boot_info->magic != KURIOS_BOOT_MAGIC) {
        /* Something is wrong - just halt */
        vga_clear();
        vga_puts("BOOT INFO INVALID!");
        return;
    }

    /* Check if we have framebuffer */
    if (boot_info->flags & BOOT_FLAG_FRAMEBUFFER) {
        FramebufferInfo *fb = (FramebufferInfo*)(uintptr_t)boot_info->framebuffer;
        fb_clear(fb, 0x000033);  /* Dark blue */
        fb_draw_test(fb);

        /* We're in graphics mode - can't use VGA text */
        return;
    }

    /* VGA text mode */
    vga_clear();

    vga_puts("====================================\n");
    vga_puts("  Kurios2 Kernel Started!\n");
    vga_puts("====================================\n\n");

    /* Print boot info */
    vga_puts("Boot Protocol Version: 0x");
    vga_put_hex(boot_info->version);
    vga_puts("\n");

    vga_puts("Boot Flags: 0x");
    vga_put_hex(boot_info->flags);
    if (boot_info->flags & BOOT_FLAG_BIOS) vga_puts(" [BIOS]");
    if (boot_info->flags & BOOT_FLAG_UEFI) vga_puts(" [UEFI]");
    if (boot_info->flags & BOOT_FLAG_ACPI) vga_puts(" [ACPI]");
    vga_puts("\n");

    vga_puts("Kernel loaded at: 0x");
    vga_put_hex(boot_info->kernel_phys);
    vga_puts("\n");

    vga_puts("Kernel size: 0x");
    vga_put_hex(boot_info->kernel_size);
    vga_puts(" bytes\n\n");

    /* Print memory map */
    vga_puts("Memory Map (");
    vga_put_hex(boot_info->memory_count);
    vga_puts(" entries):\n");

    MemoryMapEntry *mmap = (MemoryMapEntry*)(uintptr_t)boot_info->memory_map;
    for (uint64_t i = 0; i < boot_info->memory_count && i < 10; i++) {
        vga_puts("  ");
        vga_put_hex(mmap[i].base);
        vga_puts(" - ");
        vga_put_hex(mmap[i].base + mmap[i].length);
        vga_puts(" [");
        switch (mmap[i].type) {
            case MMAP_TYPE_USABLE:       vga_puts("USABLE"); break;
            case MMAP_TYPE_RESERVED:     vga_puts("RESERVED"); break;
            case MMAP_TYPE_ACPI_RECLAIM: vga_puts("ACPI"); break;
            case MMAP_TYPE_ACPI_NVS:     vga_puts("NVS"); break;
            default:                     vga_puts("OTHER"); break;
        }
        vga_puts("]\n");
    }

    if (boot_info->memory_count > 10) {
        vga_puts("  ... and more\n");
    }

    vga_puts("\n-- System halted --\n");
}
