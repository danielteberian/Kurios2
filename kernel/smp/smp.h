/* smp.h - Symmetric Multi-Processing Support */
#ifndef _KERNEL_SMP_SMP_H
#define _KERNEL_SMP_SMP_H

#include <stdint.h>
#include <stdbool.h>
#include "percpu.h"

/*
 * AP Trampoline configuration
 */
#define AP_TRAMPOLINE_ADDR      0x7000      /* Physical address of trampoline code */
#define AP_TRAMPOLINE_DATA      0x7F00      /* Physical address of startup data */

/* Offsets within trampoline data area */
#define AP_DATA_CR3             0x7F00      /* CR3 value (8 bytes) */
#define AP_DATA_STACK           0x7F08      /* Kernel stack top (8 bytes) */
#define AP_DATA_PERCPU          0x7F10      /* Per-CPU data pointer (8 bytes) */
#define AP_DATA_ENTRY           0x7F18      /* Entry point address (8 bytes) */

/*
 * Initialize SMP subsystem.
 * Called after ACPI/APIC initialization on the BSP.
 * This will detect and boot all APs.
 */
void smp_init(void);

/*
 * Wait for all APs to come online.
 * Returns number of online CPUs (including BSP).
 */
uint32_t smp_wait_for_aps(void);

/*
 * Get the total number of CPUs detected (may not all be online).
 */
uint32_t smp_get_cpu_count(void);

/*
 * Check if SMP is available (more than one CPU).
 */
bool smp_available(void);

/*
 * AP entry point (called from ap_trampoline.asm)
 * This is the first C function called on an AP.
 */
void ap_entry(percpu_data_t *percpu);

/*
 * Register IPI handlers for SMP.
 * Called after IDT is set up.
 */
void smp_register_ipi_handlers(void);

#ifdef DEBUG_TESTS
/* Run SMP tests */
void smp_run_tests(void);
#endif

#endif /* _KERNEL_SMP_SMP_H */
