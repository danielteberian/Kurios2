/* percpu.c - Per-CPU Data Management */

#include "percpu.h"
#include "../arch/x86_64/cpu.h"
#include "../mm/slab.h"
#include "../mm/pmm.h"
#include "../debug/debug.h"
#include "../acpi/acpi.h"
#include "../lib/string.h"

/* Per-CPU data array - indexed by logical CPU ID */
static percpu_data_t *percpu_array[MAX_CPUS];

/* BSP per-CPU data (statically allocated for early boot) */
static percpu_data_t bsp_percpu __attribute__((aligned(64)));

/* Number of CPUs initialized */
static volatile uint32_t num_cpus_online = 0;

/* SMP initialization flag */
static volatile bool smp_init_done = false;

/*
 * Initialize per-CPU data for the BSP
 */
void percpu_init_bsp(void)
{
    percpu_data_t *percpu = &bsp_percpu;
    const acpi_info_t *acpi = acpi_get_info();

    /* Zero the structure */
    memset(percpu, 0, sizeof(percpu_data_t));

    /* Set up self-pointer (must be first member for GS:0 access) */
    percpu->self = percpu;

    /* BSP is always CPU 0 */
    percpu->cpu_id = 0;
    percpu->is_bsp = true;
    percpu->online = true;

    /* Get APIC ID from ACPI if available, otherwise from LAPIC */
    if (acpi && acpi->valid && acpi->cpu_count > 0) {
        percpu->apic_id = acpi->cpus[0].apic_id;
    } else {
        /* Read APIC ID directly from the Local APIC register */
        /* We can't use lapic_get_id() here as APIC may not be initialized yet */
        percpu->apic_id = 0;  /* Will be updated later if needed */
    }

    /* Initialize scheduler state */
    spin_init(&percpu->sched_lock);
    percpu->current_thread = NULL;
    percpu->idle_thread = NULL;
    percpu->ready_queue_head = NULL;
    percpu->ready_queue_tail = NULL;
    percpu->current_slice = 0;

    /* Store in percpu array */
    percpu_array[0] = percpu;
    num_cpus_online = 1;

    /* Set GS_BASE MSR to point to our per-CPU data */
    write_msr(MSR_GS_BASE, (uint64_t)percpu);

    INFO("Per-CPU: BSP (CPU 0, APIC %u) initialized at 0x%llx",
         percpu->apic_id, (uint64_t)percpu);
}

/*
 * Allocate per-CPU data for an AP (Application Processor)
 */
percpu_data_t *percpu_alloc_ap(uint32_t cpu_id, uint8_t apic_id)
{
    if (cpu_id >= MAX_CPUS) {
        ERROR("Per-CPU: CPU ID %u exceeds maximum %u", cpu_id, MAX_CPUS);
        return NULL;
    }

    if (percpu_array[cpu_id] != NULL) {
        WARN("Per-CPU: CPU %u already allocated", cpu_id);
        return percpu_array[cpu_id];
    }

    /* Allocate per-CPU data structure from kernel heap
     * This returns a properly mapped virtual address, unlike alloc_pages()
     * which returns a physical address that would need manual mapping.
     *
     * Note: percpu_data_t is LARGE (~80KB with stacks), so we use kmalloc
     * which handles large allocations via the slab allocator.
     */
    percpu_data_t *percpu = (percpu_data_t *)kmalloc(sizeof(percpu_data_t));

    if (!percpu) {
        ERROR("Per-CPU: Failed to allocate memory for CPU %u", cpu_id);
        return NULL;
    }

    /* Zero the structure */
    memset(percpu, 0, sizeof(percpu_data_t));

    /* Set up self-pointer */
    percpu->self = percpu;

    /* CPU identification */
    percpu->cpu_id = cpu_id;
    percpu->apic_id = apic_id;
    percpu->is_bsp = false;
    percpu->online = false;  /* Will be set true when AP starts */

    /* Initialize scheduler state */
    spin_init(&percpu->sched_lock);
    percpu->current_thread = NULL;
    percpu->idle_thread = NULL;
    percpu->ready_queue_head = NULL;
    percpu->ready_queue_tail = NULL;
    percpu->current_slice = 0;

    /* Store in percpu array */
    percpu_array[cpu_id] = percpu;

    DEBUG("Per-CPU: Allocated CPU %u (APIC %u) at 0x%llx",
          cpu_id, apic_id, (uint64_t)percpu);

    return percpu;
}

/*
 * Get per-CPU data for a specific CPU
 */
percpu_data_t *percpu_get_cpu(uint32_t cpu_id)
{
    if (cpu_id >= MAX_CPUS) {
        return NULL;
    }
    return percpu_array[cpu_id];
}

/*
 * Get the number of online CPUs
 */
uint32_t cpu_count(void)
{
    return num_cpus_online;
}

/*
 * Increment online CPU count (called when AP comes online)
 */
void percpu_cpu_online(void)
{
    __atomic_fetch_add(&num_cpus_online, 1, __ATOMIC_SEQ_CST);
}

/*
 * Check if SMP is initialized
 */
bool smp_initialized(void)
{
    return smp_init_done;
}

/*
 * Mark SMP as initialized (called from smp.c)
 */
void percpu_set_smp_init_done(void)
{
    smp_init_done = true;
}
