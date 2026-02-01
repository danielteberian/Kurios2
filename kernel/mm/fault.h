/* fault.h - Page Fault Handler */
#ifndef _KERNEL_MM_FAULT_H
#define _KERNEL_MM_FAULT_H

#include <stdint.h>
#include <stdbool.h>
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/cpu.h"

/*
 * Page fault error code bits
 */
#define PF_PRESENT      (1 << 0)    /* Page was present (protection fault) */
#define PF_WRITE        (1 << 1)    /* Fault was a write */
#define PF_USER         (1 << 2)    /* Fault occurred in user mode */
#define PF_RESERVED     (1 << 3)    /* Reserved bit set in PTE */
#define PF_INSTRUCTION  (1 << 4)    /* Instruction fetch */

/*
 * Initialize page fault handler
 */
void fault_init(void);

/*
 * Page fault handler
 * Called from IDT exception handler
 *
 * @param state  CPU state at time of fault
 */
void page_fault_handler(cpu_state_t *state);

/*
 * Handle a COW (Copy-on-Write) fault
 * Called when a write to a COW page is attempted
 *
 * @param fault_addr  Address that caused the fault
 * @return 0 on success, -1 on failure (should kill process)
 */
int handle_cow_fault(uint64_t fault_addr);

/*
 * Handle a demand page fault
 * Called when accessing an unmapped page in a valid VMA
 *
 * @param fault_addr  Address that caused the fault
 * @return 0 on success, -1 on failure (should kill process)
 */
int handle_demand_fault(uint64_t fault_addr);

#endif /* _KERNEL_MM_FAULT_H */
