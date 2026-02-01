/* tlb.h - TLB Shootdown Support */
#ifndef _KERNEL_SMP_TLB_H
#define _KERNEL_SMP_TLB_H

#include <stdint.h>

/*
 * Initialize TLB shootdown subsystem.
 * Must be called after SMP is initialized.
 */
void tlb_init(void);

/*
 * Perform TLB shootdown for a single page.
 * This invalidates the page on all CPUs.
 *
 * @param virt  Virtual address to invalidate (page-aligned)
 */
void tlb_shootdown_page(uint64_t virt);

/*
 * Perform TLB shootdown for a range of pages.
 *
 * @param virt  Starting virtual address (page-aligned)
 * @param pages Number of pages to invalidate
 */
void tlb_shootdown_range(uint64_t virt, uint64_t pages);

/*
 * Perform full TLB flush on all CPUs.
 * Use sparingly - very expensive.
 */
void tlb_shootdown_all(void);

/*
 * TLB shootdown IPI handler.
 * Called when a CPU receives an IPI_VECTOR_TLB interrupt.
 */
void tlb_shootdown_handler(void *cpu_state);

#endif /* _KERNEL_SMP_TLB_H */
