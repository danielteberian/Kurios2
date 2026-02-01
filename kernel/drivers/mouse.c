/* mouse.c - PS/2 Mouse Driver */

#include "mouse.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/cpu.h"
#include "../sync/spinlock.h"
#include "../debug/debug.h"

/* PS/2 Controller ports */
#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_CMD_PORT        0x64

/* PS/2 Controller commands */
#define PS2_CMD_READ_CFG    0x20    /* Read configuration byte */
#define PS2_CMD_WRITE_CFG   0x60    /* Write configuration byte */
#define PS2_CMD_DISABLE_AUX 0xA7    /* Disable auxiliary device */
#define PS2_CMD_ENABLE_AUX  0xA8    /* Enable auxiliary device */
#define PS2_CMD_TEST_AUX    0xA9    /* Test auxiliary device */
#define PS2_CMD_WRITE_AUX   0xD4    /* Write to auxiliary device */

/* PS/2 Status bits */
#define PS2_STATUS_OUTPUT   0x01    /* Output buffer full */
#define PS2_STATUS_INPUT    0x02    /* Input buffer full */
#define PS2_STATUS_AUX      0x20    /* Data is from auxiliary device */

/* PS/2 Configuration bits */
#define PS2_CFG_AUX_IRQ     0x02    /* Enable IRQ12 for auxiliary device */
#define PS2_CFG_AUX_CLOCK   0x20    /* Auxiliary device clock */

/* Mouse commands */
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_RESEND        0xFE
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_DISABLE       0xF5
#define MOUSE_CMD_ENABLE        0xF4
#define MOUSE_CMD_SET_RATE      0xF3
#define MOUSE_CMD_GET_ID        0xF2
#define MOUSE_CMD_SET_STREAM    0xEA
#define MOUSE_CMD_STATUS_REQ    0xE9
#define MOUSE_CMD_RESOLUTION    0xE8

/* Mouse responses */
#define MOUSE_ACK           0xFA
#define MOUSE_SELF_TEST_OK  0xAA

/* Mouse event buffer */
#define MOUSE_BUFFER_SIZE   32

static volatile mouse_event_t mouse_buffer[MOUSE_BUFFER_SIZE];
static volatile uint8_t mouse_read_idx = 0;
static volatile uint8_t mouse_write_idx = 0;
static spinlock_t mouse_lock = SPINLOCK_INIT;

/* Mouse packet state */
static volatile uint8_t mouse_packet[3];
static volatile uint8_t mouse_packet_idx = 0;
static volatile uint8_t mouse_buttons = 0;

/* Driver state */
static volatile bool mouse_initialized = false;

/*
 * Wait for PS/2 controller input buffer to be empty
 */
static void ps2_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT) == 0) {
            return;
        }
    }
}

/*
 * Wait for PS/2 controller output buffer to have data
 */
static bool ps2_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) {
            return true;
        }
    }
    return false;
}

/*
 * Send command to PS/2 controller
 */
static void ps2_send_cmd(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_CMD_PORT, cmd);
}

/*
 * Send data to PS/2 controller
 */
static void ps2_send_data(uint8_t data) {
    ps2_wait_write();
    outb(PS2_DATA_PORT, data);
}

/*
 * Read data from PS/2 controller
 */
static uint8_t ps2_read_data(void) {
    if (!ps2_wait_read()) {
        return 0xFF;
    }
    return inb(PS2_DATA_PORT);
}

/*
 * Send command to mouse (via auxiliary device)
 */
static uint8_t mouse_send_cmd(uint8_t cmd) {
    ps2_send_cmd(PS2_CMD_WRITE_AUX);
    ps2_send_data(cmd);
    return ps2_read_data();  /* Wait for ACK */
}

/*
 * Process a complete 3-byte mouse packet
 */
static void mouse_process_packet(void) {
    /* Packet format:
     * Byte 0: Status (buttons, overflow, sign bits)
     *   Bit 0: Left button
     *   Bit 1: Right button
     *   Bit 2: Middle button
     *   Bit 3: Always 1
     *   Bit 4: X sign bit
     *   Bit 5: Y sign bit
     *   Bit 6: X overflow
     *   Bit 7: Y overflow
     * Byte 1: X movement (delta)
     * Byte 2: Y movement (delta)
     */
    uint8_t status = mouse_packet[0];
    int16_t delta_x = mouse_packet[1];
    int16_t delta_y = mouse_packet[2];

    /* Validate packet (bit 3 should always be 1) */
    if (!(status & 0x08)) {
        /* Invalid packet, resync */
        mouse_packet_idx = 0;
        return;
    }

    /* Apply sign extension if sign bits are set */
    if (status & 0x10) {
        delta_x |= 0xFF00;  /* Sign extend X */
    }
    if (status & 0x20) {
        delta_y |= 0xFF00;  /* Sign extend Y */
    }

    /* Invert Y axis (PS/2 has positive = up, we want positive = down) */
    delta_y = -delta_y;

    /* Check for overflow - ignore packet if overflow occurred */
    if ((status & 0xC0) != 0) {
        return;
    }

    /* Update button state */
    mouse_buttons = status & 0x07;

    /* Add event to buffer */
    uint8_t next_idx = (mouse_write_idx + 1) % MOUSE_BUFFER_SIZE;
    if (next_idx != mouse_read_idx) {
        mouse_buffer[mouse_write_idx].delta_x = delta_x;
        mouse_buffer[mouse_write_idx].delta_y = delta_y;
        mouse_buffer[mouse_write_idx].buttons = mouse_buttons;
        mouse_write_idx = next_idx;
    }
    /* else: buffer full, drop event */
}

/*
 * IRQ12 handler - called when mouse sends data
 */
void mouse_irq_handler(void) {
    if (!mouse_initialized) {
        /* Drain any data */
        inb(PS2_DATA_PORT);
        return;
    }

    uint8_t status = inb(PS2_STATUS_PORT);

    /* Check if data is from auxiliary device (mouse) */
    if (!(status & PS2_STATUS_AUX)) {
        return;  /* Not from mouse */
    }

    /* Read the byte */
    uint8_t data = inb(PS2_DATA_PORT);

    /* Accumulate packet bytes */
    mouse_packet[mouse_packet_idx++] = data;

    /* Check if we have a complete packet */
    if (mouse_packet_idx >= 3) {
        mouse_process_packet();
        mouse_packet_idx = 0;
    }
}

/*
 * Initialize PS/2 mouse
 */
void mouse_init(void) {
    uint8_t config;
    uint8_t response;

    INFO("Initializing PS/2 mouse driver...");

    /* Disable mouse while configuring */
    ps2_send_cmd(PS2_CMD_DISABLE_AUX);

    /* Flush any pending data */
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) {
        inb(PS2_DATA_PORT);
    }

    /* Read controller configuration */
    ps2_send_cmd(PS2_CMD_READ_CFG);
    config = ps2_read_data();

    /* Enable auxiliary device IRQ (IRQ12) and enable clock */
    config |= PS2_CFG_AUX_IRQ;
    config &= ~PS2_CFG_AUX_CLOCK;  /* Clear clock disable bit */

    /* Write back configuration */
    ps2_send_cmd(PS2_CMD_WRITE_CFG);
    ps2_send_data(config);

    /* Enable auxiliary device */
    ps2_send_cmd(PS2_CMD_ENABLE_AUX);

    /* Set mouse to default settings */
    response = mouse_send_cmd(MOUSE_CMD_SET_DEFAULTS);
    if (response != MOUSE_ACK) {
        WARN("Mouse: SET_DEFAULTS failed (response 0x%02x)", response);
    }

    /* Enable mouse data reporting */
    response = mouse_send_cmd(MOUSE_CMD_ENABLE);
    if (response != MOUSE_ACK) {
        WARN("Mouse: ENABLE failed (response 0x%02x)", response);
    }

    /* Register IRQ12 handler */
    idt_register_handler(44, (interrupt_handler_t)mouse_irq_handler);

    mouse_initialized = true;
    INFO("PS/2 mouse driver initialized (IRQ12)");
}

/*
 * Check if a mouse event is available
 */
bool mouse_has_event(void) {
    return mouse_read_idx != mouse_write_idx;
}

/*
 * Get the next mouse event (non-blocking)
 */
bool mouse_get_event(mouse_event_t *event) {
    if (!event || !mouse_has_event()) {
        return false;
    }

    uint64_t flags = spin_lock_irqsave(&mouse_lock);

    if (mouse_read_idx == mouse_write_idx) {
        spin_unlock_irqrestore(&mouse_lock, flags);
        return false;
    }

    *event = mouse_buffer[mouse_read_idx];
    mouse_read_idx = (mouse_read_idx + 1) % MOUSE_BUFFER_SIZE;

    spin_unlock_irqrestore(&mouse_lock, flags);
    return true;
}

/*
 * Get current button state
 */
uint8_t mouse_get_buttons(void) {
    return mouse_buttons;
}
