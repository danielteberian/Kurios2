/* keyboard.c - PS/2 Keyboard Driver */

#include "keyboard.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/cpu.h"
#include "../sync/spinlock.h"
#include "../debug/debug.h"

/* Keyboard I/O ports */
#define KBD_DATA_PORT    0x60
#define KBD_STATUS_PORT  0x64
#define KBD_CMD_PORT     0x64

/* Keyboard buffer */
#define KBD_BUFFER_SIZE  64

static volatile uint8_t kbd_buffer[KBD_BUFFER_SIZE];
static volatile uint8_t kbd_read_idx = 0;
static volatile uint8_t kbd_write_idx = 0;
static spinlock_t kbd_lock = SPINLOCK_INIT;

/* Modifier key state */
static volatile bool shift_pressed = false;
static volatile bool ctrl_pressed = false;
static volatile bool alt_pressed = false;
static volatile bool caps_lock = false;
static volatile bool extended_key = false;  /* E0 prefix received */

/* Special scancodes */
/* Special scancodes */
#define SC_LSHIFT_PRESS   0x2A
#define SC_LSHIFT_RELEASE 0xAA
#define SC_RSHIFT_PRESS   0x36
#define SC_RSHIFT_RELEASE 0xB6
#define SC_CTRL_PRESS     0x1D
#define SC_CTRL_RELEASE   0x9D
#define SC_ALT_PRESS      0x38
#define SC_ALT_RELEASE    0xB8
#define SC_CAPS_LOCK      0x3A
#define SC_EXTENDED       0xE0

/* Extended key scancodes (after 0xE0 prefix) */
#define SC_EXT_UP         0x48
#define SC_EXT_DOWN       0x50
#define SC_EXT_LEFT       0x4B
#define SC_EXT_RIGHT      0x4D
#define SC_EXT_DELETE     0x53
#define SC_EXT_HOME       0x47
#define SC_EXT_END        0x4F
#define SC_EXT_PGUP       0x49
#define SC_EXT_PGDN       0x51
#define SC_EXT_INSERT     0x52

/* Special key codes (returned as negative chars or high values) */
#define KEY_UP            0x80
#define KEY_DOWN          0x81
#define KEY_LEFT          0x82
#define KEY_RIGHT         0x83
#define KEY_DELETE        0x7F
#define KEY_HOME          0x84
#define KEY_END           0x85
#define KEY_PGUP          0x86
#define KEY_PGDN          0x87
#define KEY_INSERT        0x88

/* US keyboard scancode to ASCII (lowercase) */
static const char scancode_to_ascii[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   '7', '8', '9', '-', '4', '5', '6', '+', '1', '2',
    '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

/* US keyboard scancode to ASCII (uppercase/shifted) */
static const char scancode_to_ascii_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,
};

/*
 * Convert scancode to ASCII character
 */
static char scancode_to_char(uint8_t scancode) {
    if (scancode >= 128) {
        return 0;  /* Key release, ignore */
    }

    char c;
    bool use_shift = shift_pressed;

    /* Caps lock affects only letters */
    if (caps_lock) {
        char lower = scancode_to_ascii[scancode];
        if (lower >= 'a' && lower <= 'z') {
            use_shift = !use_shift;
        }
    }

    if (use_shift) {
        c = scancode_to_ascii_shift[scancode];
    } else {
        c = scancode_to_ascii[scancode];
    }

    return c;
}

/*
 * Add character to keyboard buffer
 */
static void kbd_buffer_put(uint8_t c) {
    uint8_t next = (kbd_write_idx + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_read_idx) {  /* Buffer not full */
        kbd_buffer[kbd_write_idx] = c;
        kbd_write_idx = next;
    }
}

/*
 * Keyboard interrupt handler (IRQ1)
 */
static void keyboard_handler(cpu_state_t *state) {
    (void)state;

    uint8_t scancode = inb(KBD_DATA_PORT);

    /* Handle extended scancode prefix */
    if (scancode == SC_EXTENDED) {
        extended_key = true;
        return;
    }

    /* Handle extended keys */
    if (extended_key) {
        extended_key = false;

        /* Extended key releases */
        if (scancode & 0x80) {
            uint8_t released = scancode & 0x7F;
            if (released == SC_CTRL_PRESS) ctrl_pressed = false;
            if (released == SC_ALT_PRESS) alt_pressed = false;
            return;
        }

        /* Extended key presses */
        switch (scancode) {
            case SC_CTRL_PRESS:  ctrl_pressed = true; return;
            case SC_ALT_PRESS:   alt_pressed = true; return;
            case SC_EXT_UP:      kbd_buffer_put(KEY_UP); return;
            case SC_EXT_DOWN:    kbd_buffer_put(KEY_DOWN); return;
            case SC_EXT_LEFT:    kbd_buffer_put(KEY_LEFT); return;
            case SC_EXT_RIGHT:   kbd_buffer_put(KEY_RIGHT); return;
            case SC_EXT_DELETE:  kbd_buffer_put(KEY_DELETE); return;
            case SC_EXT_HOME:    kbd_buffer_put(KEY_HOME); return;
            case SC_EXT_END:     kbd_buffer_put(KEY_END); return;
            case SC_EXT_PGUP:    kbd_buffer_put(KEY_PGUP); return;
            case SC_EXT_PGDN:    kbd_buffer_put(KEY_PGDN); return;
            case SC_EXT_INSERT:  kbd_buffer_put(KEY_INSERT); return;
        }
        return;
    }

    /* Handle modifier keys */
    switch (scancode) {
        case SC_LSHIFT_PRESS:
        case SC_RSHIFT_PRESS:
            shift_pressed = true;
            return;
        case SC_LSHIFT_RELEASE:
        case SC_RSHIFT_RELEASE:
            shift_pressed = false;
            return;
        case SC_CTRL_PRESS:
            ctrl_pressed = true;
            return;
        case SC_CTRL_RELEASE:
            ctrl_pressed = false;
            return;
        case SC_ALT_PRESS:
            alt_pressed = true;
            return;
        case SC_ALT_RELEASE:
            alt_pressed = false;
            return;
        case SC_CAPS_LOCK:
            caps_lock = !caps_lock;
            return;
    }

    /* Ignore key releases */
    if (scancode & 0x80) {
        return;
    }

    /* Convert to ASCII and buffer */
    char c = scancode_to_char(scancode);
    if (c != 0) {
        kbd_buffer_put(c);
    }
}

/*
 * Initialize keyboard driver
 */
void keyboard_init(void) {
    INFO("Initializing keyboard driver...");

    /* Clear buffer */
    kbd_read_idx = 0;
    kbd_write_idx = 0;

    /* Register IRQ1 handler */
    idt_register_handler(IRQ_KEYBOARD, (interrupt_handler_t)keyboard_handler);

    /* Enable keyboard IRQ (unmask IRQ1 on PIC) */
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 1);  /* Clear bit 1 to unmask IRQ1 */
    outb(0x21, mask);

    INFO("Keyboard driver initialized (IRQ1 enabled)");
}

/*
 * Check if keyboard buffer has data
 */
bool keyboard_has_key(void) {
    return kbd_read_idx != kbd_write_idx;
}

/*
 * Get character from buffer (non-blocking)
 */
char keyboard_getchar_nonblock(void) {
    if (kbd_read_idx == kbd_write_idx) {
        return 0;
    }

    uint64_t flags = spin_lock_irqsave(&kbd_lock);
    char c = kbd_buffer[kbd_read_idx];
    kbd_read_idx = (kbd_read_idx + 1) % KBD_BUFFER_SIZE;
    spin_unlock_irqrestore(&kbd_lock, flags);

    return c;
}

/*
 * Get character from buffer (blocking)
 */
char keyboard_getchar(void) {
    while (!keyboard_has_key()) {
        hlt();  /* Wait for interrupt */
    }
    return keyboard_getchar_nonblock();
}

/*
 * Modifier state queries
 */
bool keyboard_shift_pressed(void) { return shift_pressed; }
bool keyboard_ctrl_pressed(void) { return ctrl_pressed; }
bool keyboard_alt_pressed(void) { return alt_pressed; }
bool keyboard_caps_lock(void) { return caps_lock; }
