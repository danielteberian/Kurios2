/* percpu.h - Per-CPU Data Structure */
#ifndef _KERNEL_SMP_PERCPU_H
#define _KERNEL_SMP_PERCPU_H

#include <stdint.h>
#include <stdbool.h>
#include "../arch/x86_64/gdt.h"
#include "../sync/spinlock.h"

/* Forward declarations */
struct thread;

/* Maximum CPUs supported */
#define MAX_CPUS 256

/* Per-CPU stack sizes */
#define PERCPU_IST_STACK_SIZE   16384   /* 16KB per IST stack */
#define PERCPU_KERNEL_STACK_SIZE 16384  /* 16KB kernel stack */

/*
 * Per-CPU Data Structure
 *
 * Each CPU has its own copy of this structure.
 * Access via GS_BASE MSR (percpu_get()).
 */
typedef struct percpu_data {
    /* Self-pointer for percpu_get() - must be first */
    struct percpu_data *self;

    /* CPU identification */
    uint32_t cpu_id;            /* Logical CPU index (0 = BSP) */
    uint8_t  apic_id;           /* Local APIC ID */
    bool     is_bsp;            /* Is this the bootstrap processor? */
    volatile bool online;       /* Is this CPU online and running? */

    /* Per-CPU GDT and TSS */
    gdt_entry_t gdt[5] __attribute__((aligned(16)));
    tss_descriptor_t tss_desc __attribute__((aligned(8)));
    tss_t tss __attribute__((aligned(16)));
    gdt_pointer_t gdt_ptr;

    /* Per-CPU scheduler state */
    struct thread *current_thread;
    struct thread *idle_thread;
    struct thread *ready_queue_head;
    struct thread *ready_queue_tail;
    spinlock_t sched_lock;
    uint32_t current_slice;

    /* Per-CPU IST stacks (for critical exceptions) */
    uint8_t ist1_stack[PERCPU_IST_STACK_SIZE] __attribute__((aligned(16)));
    uint8_t ist2_stack[PERCPU_IST_STACK_SIZE] __attribute__((aligned(16)));
    uint8_t ist3_stack[PERCPU_IST_STACK_SIZE] __attribute__((aligned(16)));

    /* Per-CPU kernel stack (for syscalls/interrupts) */
    uint8_t kernel_stack[PERCPU_KERNEL_STACK_SIZE] __attribute__((aligned(16)));

    /* Padding to cache line boundary to avoid false sharing */
    uint8_t padding[64];

} __attribute__((aligned(64))) percpu_data_t;

/*
 * Initialize per-CPU data for the BSP (Bootstrap Processor).
 * Called early during kernel initialization, before SMP init.
 */
void percpu_init_bsp(void);

/*
 * Get per-CPU data for the current CPU.
 * Uses GS_BASE MSR for fast access.
 */
static inline percpu_data_t *percpu_get(void)
{
    percpu_data_t *percpu;
    /* Read the self-pointer at offset 0 from GS base */
    __asm__ volatile("movq %%gs:0, %0" : "=r"(percpu));
    return percpu;
}

/*
 * Get the current CPU's logical ID.
 */
static inline uint32_t cpu_id(void)
{
    return percpu_get()->cpu_id;
}

/*
 * Get the current CPU's APIC ID.
 */
static inline uint8_t cpu_apic_id(void)
{
    return percpu_get()->apic_id;
}

/*
 * Check if this is the BSP.
 */
static inline bool cpu_is_bsp(void)
{
    return percpu_get()->is_bsp;
}

/*
 * Check if SMP is initialized.
 */
bool smp_initialized(void);

/*
 * Get per-CPU data for a specific CPU.
 * Returns NULL if CPU doesn't exist.
 */
percpu_data_t *percpu_get_cpu(uint32_t cpu_id);

/*
 * Get the number of online CPUs.
 */
uint32_t cpu_count(void);

/*
 * Allocate and initialize per-CPU data for an AP.
 * Called by SMP init when preparing to boot an AP.
 */
percpu_data_t *percpu_alloc_ap(uint32_t cpu_id, uint8_t apic_id);

#endif /* _KERNEL_SMP_PERCPU_H */
