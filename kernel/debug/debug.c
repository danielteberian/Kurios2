/* debug.c - Kernel debugging and logging framework implementation */

#include "debug.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/io.h"

/* Global log level */
log_level_t g_log_level = LOG_INFO;

/* Output flags */
static bool serial_enabled = true;
static bool vga_enabled = false;

/* VGA text mode */
static volatile uint16_t *vga_buffer = (uint16_t*)0xB8000;
static int vga_x = 0;
static int vga_y = 0;
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_COLOR  0x0F00

/* Log level names */
static const char *level_names[] = {
    "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "FATAL"
};

/* Log level colors (ANSI escape codes for serial) */
static const char *level_colors[] = {
    "\033[90m",     /* TRACE: dark gray */
    "\033[36m",     /* DEBUG: cyan */
    "\033[32m",     /* INFO:  green */
    "\033[33m",     /* WARN:  yellow */
    "\033[31m",     /* ERROR: red */
    "\033[1;31m",   /* FATAL: bold red */
};
static const char *color_reset = "\033[0m";

/* Forward declarations */
static void vga_putc(char c);
static void output_char(char c);
static void output_string(const char *str);
static int format_print(const char *fmt, va_list args);

/* Initialize debug subsystem */
void debug_init(void) {
    serial_init_default();
    serial_enabled = true;
    vga_enabled = false;

    /* Clear VGA screen */
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = VGA_COLOR | ' ';
    }
    vga_x = 0;
    vga_y = 0;

    kprintf("\n");
    kprintf("===========================================\n");
    kprintf("  Kurios2 Kernel Debug System Initialized\n");
    kprintf("===========================================\n");
    kprintf("\n");
}

/* Set log level */
void debug_set_level(log_level_t level) {
    g_log_level = level;
}

/* Enable/disable outputs */
void debug_enable_serial(bool enable) {
    serial_enabled = enable;
}

void debug_enable_vga(bool enable) {
    vga_enabled = enable;
}

/* VGA text output */
static void vga_scroll(void) {
    for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }
    for (int i = 0; i < VGA_WIDTH; i++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = VGA_COLOR | ' ';
    }
    vga_y = VGA_HEIGHT - 1;
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\r') {
        vga_x = 0;
    } else if (c == '\t') {
        vga_x = (vga_x + 8) & ~7;
    } else if (c >= ' ') {
        vga_buffer[vga_y * VGA_WIDTH + vga_x] = VGA_COLOR | c;
        vga_x++;
    }

    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }

    if (vga_y >= VGA_HEIGHT) {
        vga_scroll();
    }
}

/* Output to enabled destinations */
static void output_char(char c) {
    if (serial_enabled) {
        debug_putc(c);
    }
    if (vga_enabled) {
        vga_putc(c);
    }
}

static void output_string(const char *str) {
    while (*str) {
        output_char(*str++);
    }
}

/* Number to string conversion */
static void print_unsigned(uint64_t value, int base, int width, char pad, bool uppercase) {
    char buf[65];
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    int pos = 64;

    buf[pos] = '\0';

    if (value == 0) {
        buf[--pos] = '0';
    } else {
        while (value > 0) {
            buf[--pos] = digits[value % base];
            value /= base;
        }
    }

    /* Padding */
    int len = 64 - pos;
    while (len < width) {
        output_char(pad);
        len++;
    }

    output_string(&buf[pos]);
}

static void print_signed(int64_t value, int width, char pad) {
    if (value < 0) {
        output_char('-');
        if (width > 0) width--;
        print_unsigned((uint64_t)(-value), 10, width, pad, false);
    } else {
        print_unsigned((uint64_t)value, 10, width, pad, false);
    }
}

/* Format string parser */
static int format_print(const char *fmt, va_list args) {
    int count = 0;

    while (*fmt) {
        if (*fmt != '%') {
            output_char(*fmt++);
            count++;
            continue;
        }

        fmt++; /* Skip '%' */

        /* Flags */
        char pad = ' ';
        bool left_justify = false;
        if (*fmt == '-') {
            left_justify = true;
            fmt++;
        }
        if (*fmt == '0' && !left_justify) {
            pad = '0';
            fmt++;
        }

        /* Width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Length modifier */
        int length = 0; /* 0=int, 1=long, 2=long long */
        if (*fmt == 'l') {
            length = 1;
            fmt++;
            if (*fmt == 'l') {
                length = 2;
                fmt++;
            }
        } else if (*fmt == 'z') {
            length = 2; /* size_t is 64-bit */
            fmt++;
        }

        /* Conversion specifier */
        switch (*fmt) {
            case 'd':
            case 'i': {
                int64_t val;
                if (length == 2) val = va_arg(args, int64_t);
                else if (length == 1) val = va_arg(args, long);
                else val = va_arg(args, int);
                print_signed(val, width, pad);
                break;
            }

            case 'u': {
                uint64_t val;
                if (length == 2) val = va_arg(args, uint64_t);
                else if (length == 1) val = va_arg(args, unsigned long);
                else val = va_arg(args, unsigned int);
                print_unsigned(val, 10, width, pad, false);
                break;
            }

            case 'x': {
                uint64_t val;
                if (length == 2) val = va_arg(args, uint64_t);
                else if (length == 1) val = va_arg(args, unsigned long);
                else val = va_arg(args, unsigned int);
                print_unsigned(val, 16, width, pad, false);
                break;
            }

            case 'X': {
                uint64_t val;
                if (length == 2) val = va_arg(args, uint64_t);
                else if (length == 1) val = va_arg(args, unsigned long);
                else val = va_arg(args, unsigned int);
                print_unsigned(val, 16, width, pad, true);
                break;
            }

            case 'p': {
                uint64_t val = (uint64_t)va_arg(args, void*);
                output_string("0x");
                print_unsigned(val, 16, 16, '0', false);
                break;
            }

            case 's': {
                const char *str = va_arg(args, const char*);
                if (str == NULL) str = "(null)";
                int len = 0;
                const char *s = str;
                while (*s++) len++;
                if (!left_justify) {
                    /* Right-justify: pad before string */
                    while (len < width) {
                        output_char(' ');
                        len++;
                    }
                }
                output_string(str);
                if (left_justify) {
                    /* Left-justify: pad after string */
                    while (len < width) {
                        output_char(' ');
                        len++;
                    }
                }
                break;
            }

            case 'c': {
                char c = (char)va_arg(args, int);
                output_char(c);
                break;
            }

            case '%':
                output_char('%');
                break;

            case 'b': {
                /* Binary (extension) */
                uint64_t val = va_arg(args, uint64_t);
                print_unsigned(val, 2, width, pad, false);
                break;
            }

            default:
                output_char('%');
                output_char(*fmt);
                break;
        }

        fmt++;
    }

    return count;
}

/* Main kprintf function */
int kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = format_print(fmt, args);
    va_end(args);
    return ret;
}

int kvprintf(const char *fmt, va_list args) {
    return format_print(fmt, args);
}

/* Log write with level, file, line */
void log_write(log_level_t level, const char *file, int line,
               const char *fmt, ...) {
    if (level < g_log_level) return;

    /* Extract filename from path */
    const char *filename = file;
    for (const char *p = file; *p; p++) {
        if (*p == '/') filename = p + 1;
    }

    /* Print colored level (serial only) */
    if (serial_enabled) {
        debug_puts(level_colors[level]);
    }

    kprintf("[%s] ", level_names[level]);

    if (serial_enabled) {
        debug_puts(color_reset);
    }

    kprintf("%s:%d: ", filename, line);

    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);

    kprintf("\n");

    /* Fatal errors trigger panic */
    if (level == LOG_FATAL) {
        panic("Fatal error logged");
    }
}

/* Panic - halt the system */
void panic(const char *fmt, ...) {
    cli();

    /* Red background for panic */
    if (serial_enabled) {
        debug_puts("\033[41;1;37m");  /* White on red */
    }

    kprintf("\n");
    kprintf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprintf("!!               KERNEL PANIC                   !!\n");
    kprintf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprintf("\n");

    if (serial_enabled) {
        debug_puts(color_reset);
    }

    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);

    kprintf("\n\n");

    /* Print basic CPU state */
    kprintf("CR2 (fault addr): 0x%016llx\n", read_cr2());
    kprintf("CR3 (page table): 0x%016llx\n", read_cr3());
    kprintf("RFLAGS:           0x%016llx\n", read_rflags());

    kprintf("\nStack trace:\n");
    stack_trace();

    kprintf("\nSystem halted.\n");

    while (1) { cli(); hlt(); }
}

/* Panic with CPU state */
void panic_with_state(const char *msg, void *cpu_state) {
    cli();

    kprintf("\n");
    kprintf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprintf("!!               KERNEL PANIC                   !!\n");
    kprintf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kprintf("\n%s\n\n", msg);

    if (cpu_state) {
        dump_registers(cpu_state);
    }

    kprintf("\nStack trace:\n");
    stack_trace();

    kprintf("\nSystem halted.\n");

    while (1) { cli(); hlt(); }
}

/* Assertion failures */
void assertion_failed(const char *expr, const char *file,
                      int line, const char *func) {
    panic("Assertion failed: %s\n"
          "  File: %s\n"
          "  Line: %d\n"
          "  Function: %s",
          expr, file, line, func);
}

void assertion_failed_msg(const char *expr, const char *msg,
                          const char *file, int line, const char *func) {
    panic("Assertion failed: %s\n"
          "  Message: %s\n"
          "  File: %s\n"
          "  Line: %d\n"
          "  Function: %s",
          expr, msg, file, line, func);
}

/* Hex dump */
void hex_dump(const void *data, size_t len, uint64_t base_addr) {
    const uint8_t *bytes = (const uint8_t *)data;

    for (size_t i = 0; i < len; i += 16) {
        kprintf("%016llx: ", base_addr + i);

        /* Hex bytes */
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                kprintf("%02x ", bytes[i + j]);
            } else {
                kprintf("   ");
            }
            if (j == 7) kprintf(" ");
        }

        kprintf(" |");

        /* ASCII */
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            char c = bytes[i + j];
            kprintf("%c", (c >= 32 && c < 127) ? c : '.');
        }

        kprintf("|\n");
    }
}

/* Stack trace */
void stack_trace(void) {
    uint64_t rbp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    stack_trace_from(rbp, 0);
}

void stack_trace_from(uint64_t rbp, uint64_t rip) {
    int frame = 0;

    if (rip != 0) {
        kprintf("  #%d: 0x%016llx\n", frame++, rip);
    }

    while (rbp != 0 && frame < 20) {
        uint64_t *frame_ptr = (uint64_t *)rbp;

        /* frame_ptr[0] = saved rbp, frame_ptr[1] = return address */
        uint64_t ret_addr = frame_ptr[1];
        uint64_t next_rbp = frame_ptr[0];

        if (ret_addr == 0) break;

        kprintf("  #%d: 0x%016llx\n", frame++, ret_addr);

        /* Sanity check to avoid infinite loops */
        if (next_rbp <= rbp) break;

        rbp = next_rbp;
    }
}

/* Register dump */
void dump_registers(void *state) {
    cpu_state_t *regs = (cpu_state_t *)state;

    kprintf("Register dump:\n");
    kprintf("  RAX=%016llx  RBX=%016llx  RCX=%016llx\n", regs->rax, regs->rbx, regs->rcx);
    kprintf("  RDX=%016llx  RSI=%016llx  RDI=%016llx\n", regs->rdx, regs->rsi, regs->rdi);
    kprintf("  RBP=%016llx  RSP=%016llx  R8 =%016llx\n", regs->rbp, regs->rsp, regs->r8);
    kprintf("  R9 =%016llx  R10=%016llx  R11=%016llx\n", regs->r9, regs->r10, regs->r11);
    kprintf("  R12=%016llx  R13=%016llx  R14=%016llx\n", regs->r12, regs->r13, regs->r14);
    kprintf("  R15=%016llx\n", regs->r15);
    kprintf("  RIP=%016llx  CS =%04llx  RFLAGS=%016llx\n", regs->rip, regs->cs, regs->rflags);
    kprintf("  SS =%04llx  INT=%llu  ERR=%016llx\n", regs->ss, regs->int_no, regs->error_code);
    kprintf("  CR2=%016llx (fault address)\n", read_cr2());
}
