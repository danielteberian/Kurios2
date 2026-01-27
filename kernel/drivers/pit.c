/* pit.c - Programmable Interval Timer (8253/8254) */

#include "pit.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/cpu.h"
#include "../sched/sched.h"
#include "../sched/thread.h"
#include "../debug/debug.h"

/* PIT I/O ports */
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43

/* PIT command bits */
#define PIT_CMD_CHANNEL0    (0 << 6)
#define PIT_CMD_CHANNEL1    (1 << 6)
#define PIT_CMD_CHANNEL2    (2 << 6)
#define PIT_CMD_LATCH       (0 << 4)
#define PIT_CMD_LOBYTE      (1 << 4)
#define PIT_CMD_HIBYTE      (2 << 4)
#define PIT_CMD_LOHI        (3 << 4)
#define PIT_CMD_MODE0       (0 << 1)  /* Interrupt on terminal count */
#define PIT_CMD_MODE1       (1 << 1)  /* Hardware one-shot */
#define PIT_CMD_MODE2       (2 << 1)  /* Rate generator */
#define PIT_CMD_MODE3       (3 << 1)  /* Square wave generator */
#define PIT_CMD_MODE4       (4 << 1)  /* Software triggered strobe */
#define PIT_CMD_MODE5       (5 << 1)  /* Hardware triggered strobe */
#define PIT_CMD_BINARY      (0 << 0)
#define PIT_CMD_BCD         (1 << 0)

/* Timer state */
static volatile uint64_t tick_count = 0;
static uint32_t timer_frequency = 0;

/*
 * PIT interrupt handler (IRQ0)
 */
static void pit_handler(cpu_state_t *state) {
    (void)state;
    tick_count++;

    /* Call scheduler tick if threading is initialized */
    if (thread_is_initialized()) {
        sched_tick();
    }
}

/*
 * Initialize PIT
 */
void pit_init(uint32_t frequency) {
    INFO("Initializing PIT at %u Hz...", frequency);

    /* Calculate divisor */
    uint32_t divisor = PIT_FREQUENCY / frequency;
    if (divisor > 65535) {
        divisor = 65535;  /* Max divisor */
        frequency = PIT_FREQUENCY / divisor;
        WARN("PIT frequency too low, using %u Hz", frequency);
    }
    if (divisor < 1) {
        divisor = 1;
        frequency = PIT_FREQUENCY;
        WARN("PIT frequency too high, using %u Hz", frequency);
    }

    timer_frequency = frequency;

    /* Configure channel 0: rate generator, lo/hi byte access */
    outb(PIT_COMMAND, PIT_CMD_CHANNEL0 | PIT_CMD_LOHI | PIT_CMD_MODE2 | PIT_CMD_BINARY);

    /* Send divisor (low byte first, then high byte) */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    /* Register IRQ0 handler */
    idt_register_handler(IRQ_TIMER, (interrupt_handler_t)pit_handler);

    /* Enable IRQ0 (unmask on PIC) */
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 0);  /* Clear bit 0 to unmask IRQ0 */
    outb(0x21, mask);

    INFO("PIT initialized: %u Hz (divisor %u)", frequency, divisor);
}

/*
 * Get tick count since boot
 */
uint64_t pit_get_ticks(void) {
    return tick_count;
}

/*
 * Get uptime in milliseconds
 */
uint64_t pit_get_uptime_ms(void) {
    if (timer_frequency == 0) return 0;
    return (tick_count * 1000) / timer_frequency;
}

/*
 * Sleep for specified milliseconds (busy wait)
 */
void pit_sleep_ms(uint32_t ms) {
    uint64_t target = tick_count + (ms * timer_frequency) / 1000;
    while (tick_count < target) {
        hlt();  /* Wait for interrupt */
    }
}

/*
 * Get configured frequency
 */
uint32_t pit_get_frequency(void) {
    return timer_frequency;
}
