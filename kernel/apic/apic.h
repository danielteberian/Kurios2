/* apic.h - Local APIC and I/O APIC Definitions */
#ifndef _KERNEL_APIC_H
#define _KERNEL_APIC_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/types.h"

/*
 * Local APIC Register Offsets (from base address)
 * Base address is typically 0xFEE00000 (from ACPI MADT)
 */
#define LAPIC_ID            0x020   /* Local APIC ID */
#define LAPIC_VERSION       0x030   /* Local APIC Version */
#define LAPIC_TPR           0x080   /* Task Priority Register */
#define LAPIC_APR           0x090   /* Arbitration Priority Register */
#define LAPIC_PPR           0x0A0   /* Processor Priority Register */
#define LAPIC_EOI           0x0B0   /* End of Interrupt */
#define LAPIC_RRD           0x0C0   /* Remote Read Register */
#define LAPIC_LDR           0x0D0   /* Logical Destination Register */
#define LAPIC_DFR           0x0E0   /* Destination Format Register */
#define LAPIC_SVR           0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ISR_BASE      0x100   /* In-Service Register (8 regs, 0x100-0x170) */
#define LAPIC_TMR_BASE      0x180   /* Trigger Mode Register (8 regs) */
#define LAPIC_IRR_BASE      0x200   /* Interrupt Request Register (8 regs) */
#define LAPIC_ESR           0x280   /* Error Status Register */
#define LAPIC_ICR_LO        0x300   /* Interrupt Command Register (low 32 bits) */
#define LAPIC_ICR_HI        0x310   /* Interrupt Command Register (high 32 bits) */
#define LAPIC_LVT_TIMER     0x320   /* LVT Timer Register */
#define LAPIC_LVT_THERMAL   0x330   /* LVT Thermal Sensor Register */
#define LAPIC_LVT_PERF      0x340   /* LVT Performance Counter Register */
#define LAPIC_LVT_LINT0     0x350   /* LVT LINT0 Register */
#define LAPIC_LVT_LINT1     0x360   /* LVT LINT1 Register */
#define LAPIC_LVT_ERROR     0x370   /* LVT Error Register */
#define LAPIC_TIMER_ICR     0x380   /* Timer Initial Count Register */
#define LAPIC_TIMER_CCR     0x390   /* Timer Current Count Register */
#define LAPIC_TIMER_DCR     0x3E0   /* Timer Divide Configuration Register */

/*
 * Spurious Interrupt Vector Register (SVR) bits
 */
#define LAPIC_SVR_ENABLE    (1 << 8)    /* APIC Software Enable */
#define LAPIC_SVR_VECTOR    0xFF        /* Spurious vector mask */

/*
 * LVT Entry bits
 */
#define LAPIC_LVT_MASKED    (1 << 16)   /* Interrupt masked */
#define LAPIC_LVT_LEVEL     (1 << 15)   /* Level triggered (for LINT) */
#define LAPIC_LVT_REMOTE    (1 << 14)   /* Remote IRR (read-only) */
#define LAPIC_LVT_LOW       (1 << 13)   /* Active low polarity */
#define LAPIC_LVT_PENDING   (1 << 12)   /* Delivery status (read-only) */

/* Delivery modes */
#define LAPIC_DM_FIXED      (0 << 8)    /* Fixed delivery */
#define LAPIC_DM_SMI        (2 << 8)    /* SMI */
#define LAPIC_DM_NMI        (4 << 8)    /* NMI */
#define LAPIC_DM_INIT       (5 << 8)    /* INIT */
#define LAPIC_DM_EXTINT     (7 << 8)    /* ExtINT (8259) */

/* Timer modes */
#define LAPIC_TIMER_ONESHOT     (0 << 17)
#define LAPIC_TIMER_PERIODIC    (1 << 17)
#define LAPIC_TIMER_TSCDEADLINE (2 << 17)

/* Timer divide values */
#define LAPIC_TIMER_DIV_1       0xB
#define LAPIC_TIMER_DIV_2       0x0
#define LAPIC_TIMER_DIV_4       0x1
#define LAPIC_TIMER_DIV_8       0x2
#define LAPIC_TIMER_DIV_16      0x3
#define LAPIC_TIMER_DIV_32      0x8
#define LAPIC_TIMER_DIV_64      0x9
#define LAPIC_TIMER_DIV_128     0xA

/*
 * ICR (Interrupt Command Register) bits
 *
 * Bits 8-10:  Delivery Mode (000=Fixed, 101=INIT, 110=Startup)
 * Bit 11:     Destination Mode (0=Physical, 1=Logical)
 * Bit 12:     Delivery Status (0=Idle, 1=Pending) - read only
 * Bit 14:     Level (0=De-assert, 1=Assert) - for INIT IPI
 * Bit 15:     Trigger Mode (0=Edge, 1=Level)
 * Bits 18-19: Destination Shorthand
 */
#define LAPIC_ICR_BUSY          (1 << 12)   /* Delivery status (read-only) */
#define LAPIC_ICR_INIT          (5 << 8)    /* INIT IPI */
#define LAPIC_ICR_STARTUP       (6 << 8)    /* Startup IPI */
#define LAPIC_ICR_ASSERT        (1 << 14)   /* Level: Assert */
#define LAPIC_ICR_DEASSERT      (0 << 14)   /* Level: De-assert */
#define LAPIC_ICR_TRIGGER_LEVEL (1 << 15)   /* Trigger mode: Level */
#define LAPIC_ICR_TRIGGER_EDGE  (0 << 15)   /* Trigger mode: Edge */
#define LAPIC_ICR_DEST_SELF     (1 << 18)   /* Self */
#define LAPIC_ICR_DEST_ALL      (2 << 18)   /* All including self */
#define LAPIC_ICR_DEST_OTHERS   (3 << 18)   /* All excluding self */

/* Backwards compatibility - LEVEL was conflated with ASSERT */
#define LAPIC_ICR_LEVEL         LAPIC_ICR_ASSERT

/*
 * I/O APIC Register Addresses
 * Access via IOREGSEL (index) and IOWIN (data) registers
 */
#define IOAPIC_REGSEL       0x00    /* Register select (index) */
#define IOAPIC_WIN          0x10    /* Register data window */

/* I/O APIC registers (accessed via REGSEL/WIN) */
#define IOAPIC_ID           0x00    /* I/O APIC ID */
#define IOAPIC_VER          0x01    /* I/O APIC Version */
#define IOAPIC_ARB          0x02    /* Arbitration ID */
#define IOAPIC_REDTBL_BASE  0x10    /* Redirection table base (entries at 0x10 + 2*n) */

/*
 * I/O APIC Redirection Entry bits (64-bit entry)
 */
#define IOAPIC_REDIR_MASKED     (1ULL << 16)    /* Interrupt masked */
#define IOAPIC_REDIR_LEVEL      (1ULL << 15)    /* Level triggered */
#define IOAPIC_REDIR_EDGE       (0ULL << 15)    /* Edge triggered */
#define IOAPIC_REDIR_LOW        (1ULL << 13)    /* Active low polarity */
#define IOAPIC_REDIR_HIGH       (0ULL << 13)    /* Active high polarity */
#define IOAPIC_REDIR_LOGICAL    (1ULL << 11)    /* Logical destination mode */
#define IOAPIC_REDIR_PHYSICAL   (0ULL << 11)    /* Physical destination mode */

/* Destination APIC ID (bits 56-63 of 64-bit entry) */
#define IOAPIC_REDIR_DEST_SHIFT 56

/*
 * Interrupt vectors
 * We use vectors 32-47 for IRQs (after exceptions 0-31)
 * Spurious vector is typically 0xFF
 */
#define APIC_VECTOR_BASE    32      /* First vector for external interrupts */
#define APIC_VECTOR_TIMER   32      /* Timer interrupt vector */
#define APIC_VECTOR_KBD     33      /* Keyboard (IRQ1 -> GSI1) */
#define APIC_VECTOR_COM1    36      /* COM1 (IRQ4 -> GSI4) */
#define APIC_VECTOR_SPURIOUS 0xFF   /* Spurious interrupt vector */

/*
 * IPI (Inter-Processor Interrupt) vectors
 */
#define IPI_VECTOR_RESCHEDULE   240     /* Reschedule on target CPU */
#define IPI_VECTOR_TLB          241     /* TLB shootdown */
#define IPI_VECTOR_HALT         242     /* Halt CPU */

/*
 * Public API
 */

/* Initialize the APIC subsystem (Local APIC + I/O APIC) */
int apic_init(void);

/* Initialize Local APIC for an AP (Application Processor) */
void lapic_init_ap(void);

/* Check if APIC is available and enabled */
bool apic_is_enabled(void);

/* Get the current CPU's Local APIC ID */
uint8_t lapic_get_id(void);

/* Send End-of-Interrupt to Local APIC */
void lapic_eoi(void);

/* Enable/disable a specific IRQ via I/O APIC */
void ioapic_enable_irq(uint8_t irq);
void ioapic_disable_irq(uint8_t irq);

/* Set IRQ routing (maps IRQ to vector on destination APIC) */
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id,
                    bool level, bool low_polarity, bool masked);

/* Disable the legacy 8259 PIC */
void pic_disable(void);

/*
 * IPI (Inter-Processor Interrupt) functions
 */

/* Wait for ICR (Interrupt Command Register) to be ready */
void lapic_wait_ipi(void);

/* Send INIT IPI to a specific APIC */
void lapic_send_init(uint8_t apic_id);

/* Send INIT IPI de-assert (level-triggered) */
void lapic_send_init_deassert(void);

/* Send Startup IPI (SIPI) to a specific APIC
 * vector: page number of trampoline code (e.g., 0x07 for 0x7000) */
void lapic_send_sipi(uint8_t apic_id, uint8_t vector);

/* Send IPI to a specific APIC with a given vector */
void ipi_send(uint8_t apic_id, uint8_t vector);

/* Send IPI to all CPUs except self */
void ipi_send_all_others(uint8_t vector);

/* Send IPI to self */
void ipi_send_self(uint8_t vector);

/*
 * LAPIC Timer functions
 */

/* Vector for LAPIC timer interrupt */
#define LAPIC_TIMER_VECTOR  48

/* Initialize LAPIC timer (must be called after hpet_init for calibration) */
void lapic_timer_init(void);

/* Initialize LAPIC timer for an AP */
void lapic_timer_init_ap(void);

/* Start LAPIC timer in periodic mode with specified frequency (Hz) */
void lapic_timer_start(uint32_t hz);

/* Stop LAPIC timer */
void lapic_timer_stop(void);

/* Check if LAPIC timer is running */
bool lapic_timer_is_running(void);

/* Get LAPIC timer frequency (ticks per second) */
uint32_t lapic_timer_get_frequency(void);

#ifdef DEBUG_TESTS
/* Run APIC tests */
void apic_run_tests(void);
#endif

#endif /* _KERNEL_APIC_H */
