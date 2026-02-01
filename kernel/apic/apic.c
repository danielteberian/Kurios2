/* apic.c - Local APIC and I/O APIC Implementation */

#include "apic.h"
#include "../acpi/acpi.h"
#include "../debug/debug.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/io.h"
#include "../arch/x86_64/idt.h"
#include "../drivers/hpet.h"
#include "../sched/sched.h"
#include "../sched/thread.h"

/* Virtual addresses for APIC register access */
static volatile uint32_t *lapic_base = NULL;
static volatile uint32_t *ioapic_base = NULL;

/* APIC state */
static bool apic_enabled = false;
static uint8_t ioapic_max_entry = 0;

/*
 * Local APIC register access
 */
static inline uint32_t lapic_read(uint32_t reg)
{
    return lapic_base[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t value)
{
    lapic_base[reg / 4] = value;
    /* Read back to ensure write completes (memory barrier) */
    (void)lapic_base[LAPIC_ID / 4];
}

/*
 * I/O APIC register access (indirect via REGSEL/WIN)
 */
static inline uint32_t ioapic_read(uint32_t reg)
{
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_WIN / 4];
}

static inline void ioapic_write(uint32_t reg, uint32_t value)
{
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_WIN / 4] = value;
}

/*
 * Read a 64-bit I/O APIC redirection entry
 */
static uint64_t ioapic_read_redir(uint8_t entry)
{
    uint32_t reg = IOAPIC_REDTBL_BASE + entry * 2;
    uint64_t lo = ioapic_read(reg);
    uint64_t hi = ioapic_read(reg + 1);
    return lo | (hi << 32);
}

/*
 * Write a 64-bit I/O APIC redirection entry
 */
static void ioapic_write_redir(uint8_t entry, uint64_t value)
{
    uint32_t reg = IOAPIC_REDTBL_BASE + entry * 2;
    ioapic_write(reg, (uint32_t)value);
    ioapic_write(reg + 1, (uint32_t)(value >> 32));
}

/*
 * Disable the legacy 8259 PIC
 * We remap it first to avoid spurious interrupts, then mask all
 */
void pic_disable(void)
{
    /* Remap PIC to vectors 0x20-0x2F (out of the way) */
    /* ICW1: Initialize + ICW4 needed */
    outb(0x20, 0x11);  /* Master PIC command */
    outb(0xA0, 0x11);  /* Slave PIC command */

    /* ICW2: Vector offset */
    outb(0x21, 0x20);  /* Master: vectors 0x20-0x27 */
    outb(0xA1, 0x28);  /* Slave: vectors 0x28-0x2F */

    /* ICW3: Cascade setup */
    outb(0x21, 0x04);  /* Master: slave on IRQ2 */
    outb(0xA1, 0x02);  /* Slave: cascade identity */

    /* ICW4: 8086 mode */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /* Mask all interrupts on both PICs */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    DEBUG("APIC: Legacy 8259 PIC disabled");
}

/*
 * Initialize the Local APIC
 */
static int lapic_init(uint64_t phys_addr)
{
    /* Map Local APIC registers to virtual memory
     * Use 0xFFFFFFFF90000000+ range (outside 2MB huge page kernel mapping) */
    uint64_t virt_addr = 0xFFFFFFFF90100000UL;

    if (vmm_map_page(virt_addr, phys_addr, PTE_KERNEL_RW | PTE_PCD) != 0) {
        ERROR("APIC: Failed to map Local APIC at 0x%llx", phys_addr);
        return -1;
    }

    lapic_base = (volatile uint32_t *)virt_addr;

    /* Read APIC ID and version */
    uint32_t id = lapic_read(LAPIC_ID) >> 24;
    uint32_t version = lapic_read(LAPIC_VERSION);
    uint8_t max_lvt = ((version >> 16) & 0xFF) + 1;

    DEBUG("APIC: Local APIC ID=%u, Version=0x%x, MaxLVT=%u", id, version & 0xFF, max_lvt);

    /* Set Task Priority to 0 (accept all interrupts) */
    lapic_write(LAPIC_TPR, 0);

    /* Set Destination Format Register to flat model */
    lapic_write(LAPIC_DFR, 0xFFFFFFFF);

    /* Set Logical Destination Register */
    lapic_write(LAPIC_LDR, (1 << 24));

    /* Configure spurious interrupt vector and enable APIC */
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | APIC_VECTOR_SPURIOUS);

    /* Mask all LVT entries initially */
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LAPIC_LVT_MASKED);

    if (max_lvt >= 4) {
        lapic_write(LAPIC_LVT_PERF, LAPIC_LVT_MASKED);
    }
    if (max_lvt >= 5) {
        lapic_write(LAPIC_LVT_THERMAL, LAPIC_LVT_MASKED);
    }

    /* Clear any pending errors */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);

    /* Send EOI to clear any pending interrupts */
    lapic_write(LAPIC_EOI, 0);

    INFO("APIC: Local APIC initialized at 0x%llx (virt 0x%llx)", phys_addr, virt_addr);

    return 0;
}

/*
 * Initialize the I/O APIC
 */
static int ioapic_init(uint32_t phys_addr, uint32_t gsi_base)
{
    /* Map I/O APIC registers to virtual memory
     * Use 0xFFFFFFFF90000000+ range (outside 2MB huge page kernel mapping) */
    uint64_t virt_addr = 0xFFFFFFFF90101000UL;

    if (vmm_map_page(virt_addr, phys_addr, PTE_KERNEL_RW | PTE_PCD) != 0) {
        ERROR("APIC: Failed to map I/O APIC at 0x%x", phys_addr);
        return -1;
    }

    ioapic_base = (volatile uint32_t *)virt_addr;

    /* Read I/O APIC ID and version */
    uint32_t id = (ioapic_read(IOAPIC_ID) >> 24) & 0x0F;
    uint32_t version = ioapic_read(IOAPIC_VER);
    ioapic_max_entry = (version >> 16) & 0xFF;

    DEBUG("APIC: I/O APIC ID=%u, Version=0x%x, MaxEntry=%u, GSI base=%u",
          id, version & 0xFF, ioapic_max_entry, gsi_base);

    /* Mask all redirection entries initially */
    for (uint8_t i = 0; i <= ioapic_max_entry; i++) {
        ioapic_write_redir(i, IOAPIC_REDIR_MASKED);
    }

    INFO("APIC: I/O APIC initialized at 0x%x (virt 0x%llx), %u entries",
         phys_addr, virt_addr, ioapic_max_entry + 1);

    return 0;
}

/*
 * Configure an IRQ in the I/O APIC
 */
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id,
                    bool level, bool low_polarity, bool masked)
{
    /* Convert IRQ to GSI using ACPI overrides */
    uint32_t gsi = acpi_isa_irq_to_gsi(irq);

    if (gsi > ioapic_max_entry) {
        WARN("APIC: GSI %u exceeds I/O APIC max entry %u", gsi, ioapic_max_entry);
        return;
    }

    uint64_t entry = vector;
    entry |= ((uint64_t)dest_apic_id << IOAPIC_REDIR_DEST_SHIFT);
    entry |= IOAPIC_REDIR_PHYSICAL;  /* Physical destination mode */

    if (level) {
        entry |= IOAPIC_REDIR_LEVEL;
    }
    if (low_polarity) {
        entry |= IOAPIC_REDIR_LOW;
    }
    if (masked) {
        entry |= IOAPIC_REDIR_MASKED;
    }

    ioapic_write_redir(gsi, entry);

    DEBUG("APIC: IRQ%u -> GSI%u -> vector %u (dest=%u, %s, %s, %s)",
          irq, gsi, vector, dest_apic_id,
          level ? "level" : "edge",
          low_polarity ? "low" : "high",
          masked ? "masked" : "enabled");
}

/*
 * Enable an IRQ via I/O APIC
 */
void ioapic_enable_irq(uint8_t irq)
{
    uint32_t gsi = acpi_isa_irq_to_gsi(irq);

    if (gsi > ioapic_max_entry) {
        return;
    }

    uint64_t entry = ioapic_read_redir(gsi);
    entry &= ~IOAPIC_REDIR_MASKED;
    ioapic_write_redir(gsi, entry);
}

/*
 * Disable an IRQ via I/O APIC
 */
void ioapic_disable_irq(uint8_t irq)
{
    uint32_t gsi = acpi_isa_irq_to_gsi(irq);

    if (gsi > ioapic_max_entry) {
        return;
    }

    uint64_t entry = ioapic_read_redir(gsi);
    entry |= IOAPIC_REDIR_MASKED;
    ioapic_write_redir(gsi, entry);
}

/*
 * Send End-of-Interrupt to Local APIC
 */
void lapic_eoi(void)
{
    if (lapic_base) {
        lapic_write(LAPIC_EOI, 0);
    }
}

/*
 * Get the current CPU's Local APIC ID
 */
uint8_t lapic_get_id(void)
{
    if (lapic_base) {
        return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
    }
    return 0;
}

/*
 * Check if APIC is enabled
 */
bool apic_is_enabled(void)
{
    return apic_enabled;
}

/*
 * Configure standard IRQs via I/O APIC
 */
static void ioapic_configure_irqs(void)
{
    uint8_t bsp_id = lapic_get_id();

    /* Get IRQ flags from ACPI for proper polarity/trigger */
    uint16_t timer_flags = acpi_get_irq_flags(0);
    uint16_t kbd_flags = acpi_get_irq_flags(1);
    uint16_t com1_flags = acpi_get_irq_flags(4);

    /* Helper macro to decode ACPI flags */
    #define IS_LEVEL(flags) (((flags) & 0x0C) == 0x0C)
    #define IS_LOW(flags)   (((flags) & 0x03) == 0x03)

    /* Timer (IRQ0) - Note: ISA timer is edge-triggered, active high by default */
    ioapic_set_irq(0, APIC_VECTOR_TIMER, bsp_id,
                   IS_LEVEL(timer_flags), IS_LOW(timer_flags), false);

    /* Keyboard (IRQ1) - Edge triggered, active high */
    ioapic_set_irq(1, APIC_VECTOR_KBD, bsp_id,
                   IS_LEVEL(kbd_flags), IS_LOW(kbd_flags), false);

    /* COM1 (IRQ4) - Edge triggered, active high */
    ioapic_set_irq(4, APIC_VECTOR_COM1, bsp_id,
                   IS_LEVEL(com1_flags), IS_LOW(com1_flags), false);

    #undef IS_LEVEL
    #undef IS_LOW

    INFO("APIC: Configured IRQ routing (Timer=%u, KBD=%u, COM1=%u)",
         APIC_VECTOR_TIMER, APIC_VECTOR_KBD, APIC_VECTOR_COM1);
}

/*
 * Initialize the APIC subsystem
 */
int apic_init(void)
{
    const acpi_info_t *acpi = acpi_get_info();

    if (!acpi || !acpi->valid) {
        ERROR("APIC: ACPI info not available");
        return -1;
    }

    if (acpi->local_apic_addr == 0) {
        ERROR("APIC: Local APIC address not found in ACPI");
        return -1;
    }

    if (acpi->ioapic_count == 0) {
        ERROR("APIC: No I/O APICs found in ACPI");
        return -1;
    }

    INFO("APIC: Initializing APIC subsystem...");

    /* Disable the legacy 8259 PIC first */
    pic_disable();

    /* Initialize Local APIC */
    if (lapic_init(acpi->local_apic_addr) != 0) {
        return -1;
    }

    /* Initialize first I/O APIC (we only support one for now) */
    if (ioapic_init(acpi->ioapics[0].address, acpi->ioapics[0].gsi_base) != 0) {
        return -1;
    }

    /* Configure IRQ routing */
    ioapic_configure_irqs();

    apic_enabled = true;

    INFO("APIC: APIC subsystem initialized successfully");

    return 0;
}

/*
 * Wait for ICR to be ready (busy bit clear)
 */
void lapic_wait_ipi(void)
{
    if (!lapic_base) return;

    /* Wait for delivery status bit to clear */
    while (lapic_read(LAPIC_ICR_LO) & LAPIC_ICR_BUSY) {
        cpu_pause();
    }
}

/*
 * Send INIT IPI to a specific APIC
 */
void lapic_send_init(uint8_t apic_id)
{
    if (!lapic_base) return;

    lapic_wait_ipi();

    /* Set destination APIC ID in high part of ICR */
    lapic_write(LAPIC_ICR_HI, (uint32_t)apic_id << 24);

    /* Send INIT IPI: level=1, assert, INIT delivery mode */
    lapic_write(LAPIC_ICR_LO, LAPIC_ICR_INIT | LAPIC_ICR_LEVEL | LAPIC_ICR_ASSERT);

    lapic_wait_ipi();
}

/*
 * Send INIT IPI de-assert (broadcast)
 * Note: This is required by the MP specification for old processors.
 * Modern processors may not require it, but it doesn't hurt.
 */
void lapic_send_init_deassert(void)
{
    if (!lapic_base) return;

    lapic_wait_ipi();

    /* INIT de-assert: Level=0, Trigger Mode=Level, All including self
     * ICR value = 0x88500 (INIT + level trigger + all including self + de-assert) */
    lapic_write(LAPIC_ICR_HI, 0);
    lapic_write(LAPIC_ICR_LO, LAPIC_ICR_INIT | LAPIC_ICR_TRIGGER_LEVEL |
                LAPIC_ICR_DEASSERT | LAPIC_ICR_DEST_ALL);

    lapic_wait_ipi();
}

/*
 * Send Startup IPI (SIPI) to a specific APIC
 */
void lapic_send_sipi(uint8_t apic_id, uint8_t vector)
{
    if (!lapic_base) return;

    lapic_wait_ipi();

    /* Set destination APIC ID */
    lapic_write(LAPIC_ICR_HI, (uint32_t)apic_id << 24);

    /* Send SIPI: startup delivery mode with vector */
    lapic_write(LAPIC_ICR_LO, LAPIC_ICR_STARTUP | vector);

    lapic_wait_ipi();
}

/*
 * Send IPI to a specific APIC with a given vector
 */
void ipi_send(uint8_t apic_id, uint8_t vector)
{
    if (!lapic_base) return;

    lapic_wait_ipi();

    /* Set destination APIC ID */
    lapic_write(LAPIC_ICR_HI, (uint32_t)apic_id << 24);

    /* Send fixed IPI with vector */
    lapic_write(LAPIC_ICR_LO, LAPIC_DM_FIXED | vector);

    lapic_wait_ipi();
}

/*
 * Send IPI to all CPUs except self
 */
void ipi_send_all_others(uint8_t vector)
{
    if (!lapic_base) return;

    lapic_wait_ipi();

    /* Destination is "all excluding self" */
    lapic_write(LAPIC_ICR_HI, 0);
    lapic_write(LAPIC_ICR_LO, LAPIC_DM_FIXED | LAPIC_ICR_DEST_OTHERS | vector);

    lapic_wait_ipi();
}

/*
 * Send IPI to self
 */
void ipi_send_self(uint8_t vector)
{
    if (!lapic_base) return;

    lapic_wait_ipi();

    /* Destination is "self" */
    lapic_write(LAPIC_ICR_HI, 0);
    lapic_write(LAPIC_ICR_LO, LAPIC_DM_FIXED | LAPIC_ICR_DEST_SELF | vector);

    lapic_wait_ipi();
}

/*
 * Initialize Local APIC for an AP
 * Similar to BSP init but without remapping IRQs
 */
void lapic_init_ap(void)
{
    if (!lapic_base) return;

    /* Set Task Priority to 0 (accept all interrupts) */
    lapic_write(LAPIC_TPR, 0);

    /* Set Destination Format Register to flat model */
    lapic_write(LAPIC_DFR, 0xFFFFFFFF);

    /* Set Logical Destination Register based on CPU ID */
    uint32_t id = (lapic_read(LAPIC_ID) >> 24) & 0xFF;
    lapic_write(LAPIC_LDR, (1 << id) << 24);

    /* Configure spurious interrupt vector and enable APIC */
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | APIC_VECTOR_SPURIOUS);

    /* Mask all LVT entries */
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LAPIC_LVT_MASKED);

    /* Clear any pending errors */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);

    /* Send EOI to clear any pending interrupts */
    lapic_write(LAPIC_EOI, 0);

    DEBUG("APIC: AP Local APIC initialized (ID=%u)", id);
}

/*
 * LAPIC Timer Implementation
 *
 * The LAPIC timer is a per-CPU timer that counts down from an initial value.
 * We calibrate it using HPET to determine the tick rate.
 */

/* LAPIC timer calibration results (shared across CPUs - they use same bus) */
static uint32_t lapic_timer_ticks_per_ms = 0;
static bool lapic_timer_calibrated = false;
static bool lapic_timer_running = false;

/*
 * LAPIC timer interrupt handler
 */
static void lapic_timer_handler(cpu_state_t *state)
{
    (void)state;

    /* Send EOI first */
    lapic_eoi();

    /* Call scheduler tick */
    if (thread_is_initialized()) {
        sched_tick();
    }
}

/*
 * Calibrate LAPIC timer using HPET
 * Returns ticks per millisecond
 */
static uint32_t lapic_timer_calibrate(void)
{
    if (!hpet_is_available()) {
        WARN("LAPIC Timer: HPET not available for calibration, using estimate");
        /* Fallback: assume 100 MHz bus clock / 16 divider = 6.25 MHz timer
         * This is a rough estimate and may not be accurate */
        return 6250;  /* ticks per ms */
    }

    /* Use HPET for accurate timing */
    DEBUG("LAPIC Timer: Calibrating using HPET...");

    /* Set up timer: divide by 16, one-shot mode, masked initially */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);

    /* Start with a large count */
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);

    /* Wait 10ms using HPET */
    hpet_delay_ms(10);

    /* Read how many ticks have elapsed */
    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);

    /* Stop the timer */
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);

    /* Calculate ticks per millisecond */
    uint32_t ticks_per_ms = elapsed / 10;

    DEBUG("LAPIC Timer: Calibrated at %u ticks/ms (elapsed=%u in 10ms)",
          ticks_per_ms, elapsed);

    return ticks_per_ms;
}

/*
 * Initialize LAPIC timer (BSP)
 */
void lapic_timer_init(void)
{
    if (!lapic_base) {
        ERROR("LAPIC Timer: Local APIC not initialized");
        return;
    }

    /* Register interrupt handler */
    idt_register_handler(LAPIC_TIMER_VECTOR, (interrupt_handler_t)lapic_timer_handler);

    /* Calibrate the timer */
    lapic_timer_ticks_per_ms = lapic_timer_calibrate();
    lapic_timer_calibrated = true;

    INFO("LAPIC Timer: Initialized, %u ticks/ms", lapic_timer_ticks_per_ms);
}

/*
 * Initialize LAPIC timer for an AP
 * Uses calibration data from BSP
 */
void lapic_timer_init_ap(void)
{
    if (!lapic_base || !lapic_timer_calibrated) {
        return;
    }

    /* APs don't need to register handler - it's shared via IDT
     * Just start the timer */
    DEBUG("LAPIC Timer: AP timer initialized");
}

/*
 * Start LAPIC timer in periodic mode
 */
void lapic_timer_start(uint32_t hz)
{
    if (!lapic_base || !lapic_timer_calibrated) {
        ERROR("LAPIC Timer: Not initialized");
        return;
    }

    /* Calculate initial count for desired frequency */
    uint32_t ticks_per_interrupt = (lapic_timer_ticks_per_ms * 1000) / hz;

    if (ticks_per_interrupt == 0) {
        ticks_per_interrupt = 1;
    }

    DEBUG("LAPIC Timer: Starting at %u Hz (initial count=%u)",
          hz, ticks_per_interrupt);

    /* Configure timer:
     * - Periodic mode
     * - Vector = LAPIC_TIMER_VECTOR
     * - Not masked */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_16);
    lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_VECTOR | LAPIC_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_ICR, ticks_per_interrupt);

    lapic_timer_running = true;
}

/*
 * Stop LAPIC timer
 */
void lapic_timer_stop(void)
{
    if (!lapic_base) {
        return;
    }

    /* Mask the timer interrupt */
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);

    lapic_timer_running = false;
}

/*
 * Check if LAPIC timer is running
 */
bool lapic_timer_is_running(void)
{
    return lapic_timer_running;
}

/*
 * Get LAPIC timer frequency (ticks per second)
 */
uint32_t lapic_timer_get_frequency(void)
{
    return lapic_timer_ticks_per_ms * 1000;
}

#ifdef DEBUG_TESTS
/*
 * Run APIC tests
 */
void apic_run_tests(void)
{
    kprintf("\n=== APIC Tests ===\n");

    /* Test 1: APIC enabled */
    kprintf("  Test 1 - APIC enabled: %s\n",
            apic_enabled ? "OK" : "FAIL");

    /* Test 2: Local APIC ID */
    uint8_t id = lapic_get_id();
    kprintf("  Test 2 - Local APIC ID: %u %s\n",
            id, (lapic_base != NULL) ? "OK" : "FAIL");

    /* Test 3: I/O APIC mapped */
    kprintf("  Test 3 - I/O APIC mapped: %s\n",
            (ioapic_base != NULL) ? "OK" : "FAIL");

    /* Test 4: I/O APIC entries */
    kprintf("  Test 4 - I/O APIC max entry: %u %s\n",
            ioapic_max_entry, (ioapic_max_entry >= 23) ? "OK" : "WARN");

    /* Test 5: Read SVR */
    if (lapic_base) {
        uint32_t svr = lapic_read(LAPIC_SVR);
        bool enabled = (svr & LAPIC_SVR_ENABLE) != 0;
        uint8_t vector = svr & LAPIC_SVR_VECTOR;
        kprintf("  Test 5 - SVR: enabled=%d, vector=0x%x %s\n",
                enabled, vector, enabled ? "OK" : "FAIL");
    }

    /* Test 6: Read timer redirection entry */
    if (ioapic_base) {
        uint32_t timer_gsi = acpi_isa_irq_to_gsi(0);
        uint64_t entry = ioapic_read_redir(timer_gsi);
        uint8_t vec = entry & 0xFF;
        bool masked = (entry & IOAPIC_REDIR_MASKED) != 0;
        kprintf("  Test 6 - Timer (GSI%u): vector=%u, masked=%d %s\n",
                timer_gsi, vec, masked, (!masked && vec == APIC_VECTOR_TIMER) ? "OK" : "FAIL");
    }

    /* Test 7: Read keyboard redirection entry */
    if (ioapic_base) {
        uint32_t kbd_gsi = acpi_isa_irq_to_gsi(1);
        uint64_t entry = ioapic_read_redir(kbd_gsi);
        uint8_t vec = entry & 0xFF;
        bool masked = (entry & IOAPIC_REDIR_MASKED) != 0;
        kprintf("  Test 7 - Keyboard (GSI%u): vector=%u, masked=%d %s\n",
                kbd_gsi, vec, masked, (!masked && vec == APIC_VECTOR_KBD) ? "OK" : "FAIL");
    }

    /* Test 8: EOI doesn't crash */
    lapic_eoi();
    kprintf("  Test 8 - EOI: OK\n");

    kprintf("\n  Summary:\n");
    kprintf("    APIC enabled: %s\n", apic_enabled ? "yes" : "no");
    kprintf("    Local APIC ID: %u\n", lapic_get_id());
    kprintf("    I/O APIC entries: %u\n", ioapic_max_entry + 1);
    kprintf("    PIC disabled: yes\n");
    kprintf("\n");
}
#endif /* DEBUG_TESTS */
