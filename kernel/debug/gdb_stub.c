/* gdb_stub.c - GDB Remote Serial Protocol stub implementation */

#include "gdb_stub.h"
#include "debug.h"
#include "../arch/x86_64/serial.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/cpu.h"
#include <stddef.h>

/* Packet buffer size */
#define GDB_PACKET_SIZE     4096

/* Breakpoint entry */
typedef struct {
    uint64_t addr;
    uint8_t original_byte;
    bool active;
} gdb_breakpoint_t;

/* GDB stub state */
static struct {
    bool initialized;
    bool connected;
    bool stepping;
    cpu_state_t *current_state;
    gdb_breakpoint_t breakpoints[GDB_MAX_BREAKPOINTS];
} gdb_state;

/* Packet buffers */
static char gdb_packet_buf[GDB_PACKET_SIZE];
static char gdb_response_buf[GDB_PACKET_SIZE];

/* Forward declarations */
static void gdb_event_loop(cpu_state_t *state, uint8_t signal);
static void gdb_handle_command(const char *cmd, cpu_state_t *state);

/*
 * Serial I/O for GDB (COM2)
 */
static void gdb_serial_init(void) {
    serial_init(GDB_SERIAL_PORT, BAUD_115200);
}

static void gdb_putc(char c) {
    serial_putc(GDB_SERIAL_PORT, c);
}

static char gdb_getc(void) {
    return serial_getc(GDB_SERIAL_PORT);
}

/*
 * Hex conversion utilities
 */
static const char hex_chars[] = "0123456789abcdef";

static int hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void byte_to_hex(uint8_t val, char *out) {
    out[0] = hex_chars[(val >> 4) & 0xF];
    out[1] = hex_chars[val & 0xF];
}

static void mem_to_hex(const uint8_t *mem, char *hex, size_t len) {
    for (size_t i = 0; i < len; i++) {
        byte_to_hex(mem[i], &hex[i * 2]);
    }
    hex[len * 2] = '\0';
}

static void hex_to_mem(const char *hex, uint8_t *mem, size_t len) {
    for (size_t i = 0; i < len; i++) {
        int high = hex_char_to_int(hex[i * 2]);
        int low = hex_char_to_int(hex[i * 2 + 1]);
        if (high < 0 || low < 0) break;
        mem[i] = (high << 4) | low;
    }
}

/* Convert 64-bit value to hex (little-endian byte order for GDB) */
static void uint64_to_hex_le(uint64_t val, char *out) {
    for (int i = 0; i < 8; i++) {
        byte_to_hex((val >> (i * 8)) & 0xFF, &out[i * 2]);
    }
}

/* Convert 32-bit value to hex (little-endian byte order for GDB) */
static void uint32_to_hex_le(uint32_t val, char *out) {
    for (int i = 0; i < 4; i++) {
        byte_to_hex((val >> (i * 8)) & 0xFF, &out[i * 2]);
    }
}

/*
 * GDB packet protocol
 */
static uint8_t gdb_checksum(const char *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += (uint8_t)data[i];
    }
    return sum;
}

static void gdb_send_packet(const char *data) {
    size_t len = 0;
    while (data[len]) len++;

    uint8_t checksum = gdb_checksum(data, len);

    gdb_putc('$');
    for (size_t i = 0; i < len; i++) {
        gdb_putc(data[i]);
    }
    gdb_putc('#');
    gdb_putc(hex_chars[(checksum >> 4) & 0xF]);
    gdb_putc(hex_chars[checksum & 0xF]);
}

static int gdb_recv_packet(char *buffer, size_t bufsize) {
    char c;

    /* Wait for packet start */
    do {
        c = gdb_getc();
        if (c == 0x03) {
            /* Ctrl+C - interrupt */
            return -1;
        }
    } while (c != '$');

    /* Receive packet data */
    size_t len = 0;
    uint8_t checksum = 0;

    while (len < bufsize - 1) {
        c = gdb_getc();
        if (c == '#') break;
        buffer[len++] = c;
        checksum += (uint8_t)c;
    }
    buffer[len] = '\0';

    /* Receive and verify checksum */
    char cs_high = gdb_getc();
    char cs_low = gdb_getc();
    uint8_t recv_checksum = (hex_char_to_int(cs_high) << 4) | hex_char_to_int(cs_low);

    if (recv_checksum == checksum) {
        gdb_putc('+');  /* ACK */
        return (int)len;
    } else {
        gdb_putc('-');  /* NAK */
        return 0;
    }
}

static void gdb_send_ok(void) {
    gdb_send_packet("OK");
}

static void gdb_send_error(int err) {
    char buf[4];
    buf[0] = 'E';
    byte_to_hex((uint8_t)err, &buf[1]);
    buf[3] = '\0';
    gdb_send_packet(buf);
}

static void gdb_send_stop_reply(uint8_t signal) {
    char buf[4];
    buf[0] = 'S';
    byte_to_hex(signal, &buf[1]);
    buf[3] = '\0';
    gdb_send_packet(buf);
}

/*
 * Register handling
 *
 * GDB x86_64 register order:
 * 0-15: rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15 (64-bit each)
 * 16: rip (64-bit)
 * 17: eflags (32-bit)
 * 18-23: cs, ss, ds, es, fs, gs (32-bit each)
 */
static void gdb_read_registers(cpu_state_t *state, char *out) {
    char *p = out;

    /* General purpose registers (64-bit each) */
    uint64_to_hex_le(state->rax, p); p += 16;
    uint64_to_hex_le(state->rbx, p); p += 16;
    uint64_to_hex_le(state->rcx, p); p += 16;
    uint64_to_hex_le(state->rdx, p); p += 16;
    uint64_to_hex_le(state->rsi, p); p += 16;
    uint64_to_hex_le(state->rdi, p); p += 16;
    uint64_to_hex_le(state->rbp, p); p += 16;
    uint64_to_hex_le(state->rsp, p); p += 16;
    uint64_to_hex_le(state->r8, p); p += 16;
    uint64_to_hex_le(state->r9, p); p += 16;
    uint64_to_hex_le(state->r10, p); p += 16;
    uint64_to_hex_le(state->r11, p); p += 16;
    uint64_to_hex_le(state->r12, p); p += 16;
    uint64_to_hex_le(state->r13, p); p += 16;
    uint64_to_hex_le(state->r14, p); p += 16;
    uint64_to_hex_le(state->r15, p); p += 16;

    /* RIP (64-bit) */
    uint64_to_hex_le(state->rip, p); p += 16;

    /* EFLAGS (32-bit) */
    uint32_to_hex_le((uint32_t)state->rflags, p); p += 8;

    /* Segment registers (32-bit each) */
    uint32_to_hex_le((uint32_t)state->cs, p); p += 8;
    uint32_to_hex_le((uint32_t)state->ss, p); p += 8;
    uint32_to_hex_le(0, p); p += 8;  /* ds */
    uint32_to_hex_le(0, p); p += 8;  /* es */
    uint32_to_hex_le(0, p); p += 8;  /* fs */
    uint32_to_hex_le(0, p); p += 8;  /* gs */

    *p = '\0';
}

/* Convert little-endian hex to uint64_t */
static uint64_t hex_le_to_uint64(const char *hex) {
    uint64_t result = 0;
    for (int i = 0; i < 8; i++) {
        int high = hex_char_to_int(hex[i * 2]);
        int low = hex_char_to_int(hex[i * 2 + 1]);
        if (high < 0 || low < 0) break;
        result |= ((uint64_t)((high << 4) | low)) << (i * 8);
    }
    return result;
}

static void gdb_write_registers(cpu_state_t *state, const char *hex) {
    const char *p = hex;

    state->rax = hex_le_to_uint64(p); p += 16;
    state->rbx = hex_le_to_uint64(p); p += 16;
    state->rcx = hex_le_to_uint64(p); p += 16;
    state->rdx = hex_le_to_uint64(p); p += 16;
    state->rsi = hex_le_to_uint64(p); p += 16;
    state->rdi = hex_le_to_uint64(p); p += 16;
    state->rbp = hex_le_to_uint64(p); p += 16;
    state->rsp = hex_le_to_uint64(p); p += 16;
    state->r8 = hex_le_to_uint64(p); p += 16;
    state->r9 = hex_le_to_uint64(p); p += 16;
    state->r10 = hex_le_to_uint64(p); p += 16;
    state->r11 = hex_le_to_uint64(p); p += 16;
    state->r12 = hex_le_to_uint64(p); p += 16;
    state->r13 = hex_le_to_uint64(p); p += 16;
    state->r14 = hex_le_to_uint64(p); p += 16;
    state->r15 = hex_le_to_uint64(p); p += 16;
    state->rip = hex_le_to_uint64(p); p += 16;

    /* EFLAGS (32-bit) - preserve upper bits */
    uint32_t eflags = 0;
    for (int i = 0; i < 4; i++) {
        int high = hex_char_to_int(p[i * 2]);
        int low = hex_char_to_int(p[i * 2 + 1]);
        if (high >= 0 && low >= 0) {
            eflags |= ((uint32_t)((high << 4) | low)) << (i * 8);
        }
    }
    state->rflags = (state->rflags & 0xFFFFFFFF00000000ULL) | eflags;
}

/*
 * Memory access
 */
static bool gdb_read_mem(uint64_t addr, uint8_t *buf, size_t len) {
    /* Simple implementation - just read directly
     * A production implementation would check page tables */
    volatile uint8_t *src = (volatile uint8_t *)addr;
    for (size_t i = 0; i < len; i++) {
        buf[i] = src[i];
    }
    return true;
}

static bool gdb_write_mem(uint64_t addr, const uint8_t *buf, size_t len) {
    volatile uint8_t *dst = (volatile uint8_t *)addr;
    for (size_t i = 0; i < len; i++) {
        dst[i] = buf[i];
    }
    return true;
}

/*
 * Breakpoint management
 */
static gdb_breakpoint_t *gdb_find_breakpoint(uint64_t addr) {
    for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        if (gdb_state.breakpoints[i].active &&
            gdb_state.breakpoints[i].addr == addr) {
            return &gdb_state.breakpoints[i];
        }
    }
    return NULL;
}

static gdb_breakpoint_t *gdb_find_free_breakpoint(void) {
    for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        if (!gdb_state.breakpoints[i].active) {
            return &gdb_state.breakpoints[i];
        }
    }
    return NULL;
}

static bool gdb_set_breakpoint(uint64_t addr) {
    /* Check if already set */
    if (gdb_find_breakpoint(addr)) {
        return true;
    }

    gdb_breakpoint_t *bp = gdb_find_free_breakpoint();
    if (!bp) {
        return false;  /* No free slots */
    }

    /* Save original byte and write INT3 (0xCC) */
    volatile uint8_t *ptr = (volatile uint8_t *)addr;
    bp->addr = addr;
    bp->original_byte = *ptr;
    bp->active = true;
    *ptr = 0xCC;

    return true;
}

static bool gdb_remove_breakpoint(uint64_t addr) {
    gdb_breakpoint_t *bp = gdb_find_breakpoint(addr);
    if (!bp) {
        return false;
    }

    /* Restore original byte */
    volatile uint8_t *ptr = (volatile uint8_t *)addr;
    *ptr = bp->original_byte;
    bp->active = false;

    return true;
}

/* Temporarily restore all breakpoints (for single-stepping) */
static void gdb_restore_breakpoints(void) {
    for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        if (gdb_state.breakpoints[i].active) {
            volatile uint8_t *ptr = (volatile uint8_t *)gdb_state.breakpoints[i].addr;
            *ptr = 0xCC;
        }
    }
}

/* Temporarily remove breakpoint at address (for continuing past it) */
static void gdb_hide_breakpoint(uint64_t addr) {
    gdb_breakpoint_t *bp = gdb_find_breakpoint(addr);
    if (bp) {
        volatile uint8_t *ptr = (volatile uint8_t *)addr;
        *ptr = bp->original_byte;
    }
}

/*
 * Command handlers
 */
static void gdb_handle_query(const char *cmd, cpu_state_t *state) {
    (void)state;

    if (cmd[0] == 'S' && cmd[1] == 'u' && cmd[2] == 'p') {
        /* qSupported - report supported features */
        gdb_send_packet("PacketSize=1000");
    } else if (cmd[0] == 'A' && cmd[1] == 't' && cmd[2] == 't') {
        /* qAttached - are we attached to an existing process? */
        gdb_send_packet("1");
    } else if (cmd[0] == 'C') {
        /* qC - current thread ID */
        gdb_send_packet("QC1");
    } else if (cmd[0] == 'f' && cmd[1] == 'T') {
        /* qfThreadInfo - first thread in list */
        gdb_send_packet("m1");
    } else if (cmd[0] == 's' && cmd[1] == 'T') {
        /* qsThreadInfo - subsequent threads */
        gdb_send_packet("l");  /* End of list */
    } else {
        /* Unknown query - send empty response */
        gdb_send_packet("");
    }
}

static void gdb_handle_read_regs(cpu_state_t *state) {
    gdb_read_registers(state, gdb_response_buf);
    gdb_send_packet(gdb_response_buf);
}

static void gdb_handle_write_regs(const char *cmd, cpu_state_t *state) {
    gdb_write_registers(state, cmd);
    gdb_send_ok();
}

static void gdb_handle_read_mem(const char *cmd) {
    /* Format: m<addr>,<len> */
    uint64_t addr = 0;
    size_t len = 0;
    const char *p = cmd;

    /* Parse address */
    while (*p && *p != ',') {
        int digit = hex_char_to_int(*p);
        if (digit < 0) break;
        addr = (addr << 4) | digit;
        p++;
    }

    if (*p == ',') p++;

    /* Parse length */
    while (*p) {
        int digit = hex_char_to_int(*p);
        if (digit < 0) break;
        len = (len << 4) | digit;
        p++;
    }

    if (len > (GDB_PACKET_SIZE - 1) / 2) {
        len = (GDB_PACKET_SIZE - 1) / 2;
    }

    /* Read memory */
    uint8_t buf[2048];
    if (len > sizeof(buf)) len = sizeof(buf);

    if (gdb_read_mem(addr, buf, len)) {
        mem_to_hex(buf, gdb_response_buf, len);
        gdb_send_packet(gdb_response_buf);
    } else {
        gdb_send_error(1);
    }
}

static void gdb_handle_write_mem(const char *cmd) {
    /* Format: M<addr>,<len>:<data> */
    uint64_t addr = 0;
    size_t len = 0;
    const char *p = cmd;

    /* Parse address */
    while (*p && *p != ',') {
        int digit = hex_char_to_int(*p);
        if (digit < 0) break;
        addr = (addr << 4) | digit;
        p++;
    }

    if (*p == ',') p++;

    /* Parse length */
    while (*p && *p != ':') {
        int digit = hex_char_to_int(*p);
        if (digit < 0) break;
        len = (len << 4) | digit;
        p++;
    }

    if (*p == ':') p++;

    /* Convert hex data to bytes */
    uint8_t buf[2048];
    if (len > sizeof(buf)) len = sizeof(buf);
    hex_to_mem(p, buf, len);

    if (gdb_write_mem(addr, buf, len)) {
        gdb_send_ok();
    } else {
        gdb_send_error(1);
    }
}

static void gdb_handle_continue(cpu_state_t *state) {
    /* Clear trap flag */
    state->rflags &= ~RFLAGS_TF;
    gdb_state.stepping = false;

    /* Re-enable breakpoints */
    gdb_restore_breakpoints();
}

static void gdb_handle_step(cpu_state_t *state) {
    /* Set trap flag for single-step */
    state->rflags |= RFLAGS_TF;
    gdb_state.stepping = true;

    /* Hide breakpoint at current location (if any) so we can step past it */
    gdb_hide_breakpoint(state->rip);
}

static void gdb_handle_breakpoint_set(const char *cmd) {
    /* Format: Z0,<addr>,<kind> */
    /* Skip "0," */
    const char *p = cmd + 2;

    uint64_t addr = 0;
    while (*p && *p != ',') {
        int digit = hex_char_to_int(*p);
        if (digit < 0) break;
        addr = (addr << 4) | digit;
        p++;
    }

    if (gdb_set_breakpoint(addr)) {
        gdb_send_ok();
    } else {
        gdb_send_error(1);
    }
}

static void gdb_handle_breakpoint_remove(const char *cmd) {
    /* Format: z0,<addr>,<kind> */
    /* Skip "0," */
    const char *p = cmd + 2;

    uint64_t addr = 0;
    while (*p && *p != ',') {
        int digit = hex_char_to_int(*p);
        if (digit < 0) break;
        addr = (addr << 4) | digit;
        p++;
    }

    if (gdb_remove_breakpoint(addr)) {
        gdb_send_ok();
    } else {
        gdb_send_error(1);
    }
}

static void gdb_handle_command(const char *cmd, cpu_state_t *state) {
    switch (cmd[0]) {
    case '?':
        /* Stop reason */
        gdb_send_stop_reply(GDB_SIGNAL_TRAP);
        break;

    case 'g':
        /* Read registers */
        gdb_handle_read_regs(state);
        break;

    case 'G':
        /* Write registers */
        gdb_handle_write_regs(&cmd[1], state);
        break;

    case 'm':
        /* Read memory */
        gdb_handle_read_mem(&cmd[1]);
        break;

    case 'M':
        /* Write memory */
        gdb_handle_write_mem(&cmd[1]);
        break;

    case 'c':
        /* Continue */
        gdb_handle_continue(state);
        return;  /* Exit event loop */

    case 's':
        /* Single step */
        gdb_handle_step(state);
        return;  /* Exit event loop */

    case 'Z':
        /* Set breakpoint */
        if (cmd[1] == '0') {
            gdb_handle_breakpoint_set(&cmd[1]);
        } else {
            gdb_send_packet("");  /* Unsupported breakpoint type */
        }
        break;

    case 'z':
        /* Remove breakpoint */
        if (cmd[1] == '0') {
            gdb_handle_breakpoint_remove(&cmd[1]);
        } else {
            gdb_send_packet("");
        }
        break;

    case 'q':
        /* Query */
        gdb_handle_query(&cmd[1], state);
        break;

    case 'H':
        /* Set thread - we only have one thread */
        gdb_send_ok();
        break;

    case 'D':
        /* Detach */
        gdb_send_ok();
        gdb_state.connected = false;
        return;

    case 'k':
        /* Kill - just continue */
        gdb_state.connected = false;
        return;

    default:
        /* Unknown command */
        gdb_send_packet("");
        break;
    }
}

/*
 * Main GDB event loop - called when stopped
 */
static void gdb_event_loop(cpu_state_t *state, uint8_t signal) {
    gdb_state.current_state = state;
    gdb_state.connected = true;

    /* Send stop reply */
    gdb_send_stop_reply(signal);

    /* Process commands until continue or step */
    while (gdb_state.connected) {
        int len = gdb_recv_packet(gdb_packet_buf, sizeof(gdb_packet_buf));

        if (len < 0) {
            /* Ctrl+C - send stop reply */
            gdb_send_stop_reply(GDB_SIGNAL_INT);
            continue;
        }

        if (len == 0) {
            /* Checksum error - packet will be resent */
            continue;
        }

        gdb_handle_command(gdb_packet_buf, state);

        /* Check if we should exit (continue/step) */
        if (!gdb_state.connected ||
            gdb_packet_buf[0] == 'c' ||
            gdb_packet_buf[0] == 's') {
            break;
        }
    }
}

/*
 * Exception handlers
 */
static void gdb_breakpoint_handler(void *cpu_state_ptr) {
    cpu_state_t *state = (cpu_state_t *)cpu_state_ptr;

    /* INT3 is 1 byte, RIP points past it. Adjust back. */
    state->rip--;

    /* Check if this is one of our breakpoints */
    gdb_breakpoint_t *bp = gdb_find_breakpoint(state->rip);
    if (bp) {
        /* Restore original byte so GDB can read the real instruction */
        volatile uint8_t *ptr = (volatile uint8_t *)state->rip;
        *ptr = bp->original_byte;
    }

    /* Enter debugger */
    gdb_event_loop(state, GDB_SIGNAL_TRAP);
}

static void gdb_debug_handler(void *cpu_state_ptr) {
    cpu_state_t *state = (cpu_state_t *)cpu_state_ptr;

    /* Clear trap flag */
    state->rflags &= ~RFLAGS_TF;

    /* Re-enable breakpoints after stepping */
    gdb_restore_breakpoints();

    /* Enter debugger */
    gdb_event_loop(state, GDB_SIGNAL_TRAP);
}

/*
 * Public API
 */
void gdb_init(void) {
    /* Initialize state */
    gdb_state.initialized = false;
    gdb_state.connected = false;
    gdb_state.stepping = false;
    gdb_state.current_state = NULL;

    for (int i = 0; i < GDB_MAX_BREAKPOINTS; i++) {
        gdb_state.breakpoints[i].active = false;
    }

    /* Initialize COM2 for GDB */
    gdb_serial_init();

    /* Register exception handlers */
    idt_register_handler(INT_DEBUG, gdb_debug_handler);
    idt_register_handler(INT_BREAKPOINT, gdb_breakpoint_handler);

    gdb_state.initialized = true;

    INFO("GDB stub initialized on COM2 (0x%x)", GDB_SERIAL_PORT);
}

void gdb_breakpoint(void) {
    if (!gdb_state.initialized) {
        return;
    }
    __asm__ volatile("int3");
}

bool gdb_is_active(void) {
    return gdb_state.connected;
}
