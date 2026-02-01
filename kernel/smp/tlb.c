/* tlb.c - TLB Shootdown Implementation */

#include "tlb.h"
#include "percpu.h"
#include "smp.h"
#include "../arch/x86_64/cpu.h"
#include "../apic/apic.h"
#include "../sync/spinlock.h"
#include "../debug/debug.h"

/*
 * TLB shootdown state - shared between all CPUs
 */
static struct {
    spinlock_t lock;            /* Protects shootdown state */
    volatile uint64_t addr;     /* Address to flush (0 = full flush) */
    volatile uint64_t pages;    /* Number of pages (0 = full flush) */
    volatile uint32_t pending;  /* Bitmask of CPUs that need to flush */
    volatile bool active;       /* Shootdown in progress */
} tlb_shootdown __attribute__((aligned(64)));

/*
 * Initialize TLB shootdown subsystem
 */
void tlb_init(void)
{
    spin_init(&tlb_shootdown.lock);
    tlb_shootdown.addr = 0;
    tlb_shootdown.pages = 0;
    tlb_shootdown.pending = 0;
    tlb_shootdown.active = false;

    /* Register TLB shootdown IPI handler */
    extern void idt_register_handler(uint8_t vector, void (*handler)(void *));
    idt_register_handler(IPI_VECTOR_TLB, tlb_shootdown_handler);

    DEBUG("TLB: Shootdown subsystem initialized");
}

/*
 * Perform local TLB flush for the given address
 */
static inline void tlb_flush_local(uint64_t addr)
{
    invlpg(addr);
}

/*
 * Perform local full TLB flush
 */
static inline void tlb_flush_local_all(void)
{
    flush_tlb();
}

/*
 * TLB shootdown IPI handler
 */
void tlb_shootdown_handler(void *cpu_state)
{
    (void)cpu_state;

    /* Acknowledge the IPI first */
    lapic_eoi();

    if (!tlb_shootdown.active) {
        return;
    }

    /* Perform the flush */
    if (tlb_shootdown.pages == 0) {
        /* Full TLB flush */
        tlb_flush_local_all();
    } else {
        /* Flush specific pages */
        uint64_t addr = tlb_shootdown.addr;
        for (uint64_t i = 0; i < tlb_shootdown.pages; i++) {
            tlb_flush_local(addr + i * 4096);
        }
    }

    /* Clear our pending bit */
    uint32_t my_bit = 1U << cpu_id();
    __atomic_fetch_and(&tlb_shootdown.pending, ~my_bit, __ATOMIC_SEQ_CST);
}

/*
 * Perform TLB shootdown for a single page
 */
void tlb_shootdown_page(uint64_t virt)
{
    tlb_shootdown_range(virt, 1);
}

/*
 * Perform TLB shootdown for a range of pages
 */
void tlb_shootdown_range(uint64_t virt, uint64_t pages)
{
    /* If single CPU, just flush locally */
    if (cpu_count() <= 1) {
        for (uint64_t i = 0; i < pages; i++) {
            tlb_flush_local(virt + i * 4096);
        }
        return;
    }

    uint64_t flags = spin_lock_irqsave(&tlb_shootdown.lock);

    /* Set up shootdown parameters */
    tlb_shootdown.addr = virt;
    tlb_shootdown.pages = pages;

    /* Build pending bitmask of all online CPUs except us */
    uint32_t online_cpus = cpu_count();
    uint32_t pending_mask = 0;
    for (uint32_t i = 0; i < online_cpus; i++) {
        if (i != cpu_id()) {
            percpu_data_t *pcpu = percpu_get_cpu(i);
            if (pcpu && pcpu->online) {
                pending_mask |= (1U << i);
            }
        }
    }

    tlb_shootdown.pending = pending_mask;
    tlb_shootdown.active = true;

    /* Flush locally first */
    for (uint64_t i = 0; i < pages; i++) {
        tlb_flush_local(virt + i * 4096);
    }

    /* If there are other CPUs, send IPI */
    if (pending_mask != 0) {
        /* Send IPI to all other CPUs */
        ipi_send_all_others(IPI_VECTOR_TLB);

        /* Wait for all CPUs to complete */
        while (tlb_shootdown.pending != 0) {
            cpu_pause();
        }
    }

    tlb_shootdown.active = false;

    spin_unlock_irqrestore(&tlb_shootdown.lock, flags);
}

/*
 * Perform full TLB flush on all CPUs
 */
void tlb_shootdown_all(void)
{
    /* If single CPU, just flush locally */
    if (cpu_count() <= 1) {
        tlb_flush_local_all();
        return;
    }

    uint64_t flags = spin_lock_irqsave(&tlb_shootdown.lock);

    /* Set up full flush */
    tlb_shootdown.addr = 0;
    tlb_shootdown.pages = 0;  /* 0 pages means full flush */

    /* Build pending bitmask */
    uint32_t online_cpus = cpu_count();
    uint32_t pending_mask = 0;
    for (uint32_t i = 0; i < online_cpus; i++) {
        if (i != cpu_id()) {
            percpu_data_t *pcpu = percpu_get_cpu(i);
            if (pcpu && pcpu->online) {
                pending_mask |= (1U << i);
            }
        }
    }

    tlb_shootdown.pending = pending_mask;
    tlb_shootdown.active = true;

    /* Flush locally first */
    tlb_flush_local_all();

    /* If there are other CPUs, send IPI */
    if (pending_mask != 0) {
        ipi_send_all_others(IPI_VECTOR_TLB);

        /* Wait for all CPUs to complete */
        while (tlb_shootdown.pending != 0) {
            cpu_pause();
        }
    }

    tlb_shootdown.active = false;

    spin_unlock_irqrestore(&tlb_shootdown.lock, flags);
}
