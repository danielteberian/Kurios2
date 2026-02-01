/* smp.c - Symmetric Multi-Processing Implementation */

#include "smp.h"
#include "percpu.h"
#include "../arch/x86_64/cpu.h"
#include "../arch/x86_64/gdt.h"
#include "../arch/x86_64/idt.h"
#include "../acpi/acpi.h"
#include "../apic/apic.h"
#include "../drivers/hpet.h"
#include "../drivers/pit.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "../sched/sched.h"
#include "../sched/thread.h"

/* External: AP trampoline code boundaries (from ap_trampoline.asm) */
extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];

/* Total number of CPUs detected */
static uint32_t total_cpu_count = 1;

/* Number of APs successfully booted */
static volatile uint32_t aps_booted = 0;

/* Flag to signal APs to continue initialization */
static volatile bool ap_can_continue = false;

/*
 * Delay using available timer
 */
static void smp_delay_us(uint32_t us)
{
    if (hpet_is_available()) {
        hpet_delay_us(us);
    } else {
        /* Fallback to busy loop - very approximate */
        volatile uint64_t count = us * 1000;
        while (count--) {
            cpu_pause();
        }
    }
}

static void smp_delay_ms(uint32_t ms)
{
    smp_delay_us(ms * 1000);
}

/*
 * Copy AP trampoline to low memory
 */
static void copy_trampoline(void)
{
    uint8_t *src = ap_trampoline_start;
    uint8_t *dst = (uint8_t *)AP_TRAMPOLINE_ADDR;
    size_t size = ap_trampoline_end - ap_trampoline_start;

    DEBUG("SMP: Trampoline source at 0x%llx, size %llu bytes",
          (unsigned long long)(uint64_t)src, (unsigned long long)size);
    DEBUG("SMP: Copying trampoline to 0x%x", AP_TRAMPOLINE_ADDR);

    /* Copy byte by byte to ensure it works */
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }

    /* Verify the copy */
    DEBUG("SMP: Trampoline first bytes: %02x %02x %02x %02x",
          dst[0], dst[1], dst[2], dst[3]);
}

/*
 * Set up data for AP startup
 */
static void setup_ap_data(percpu_data_t *percpu)
{
    volatile uint64_t *data = (volatile uint64_t *)AP_TRAMPOLINE_DATA;

    /* CR3: Use the same page tables as BSP */
    data[0] = read_cr3();

    /* Stack: Top of per-CPU kernel stack */
    data[1] = (uint64_t)(percpu->kernel_stack + PERCPU_KERNEL_STACK_SIZE);

    /* Per-CPU data pointer */
    data[2] = (uint64_t)percpu;

    /* Entry point */
    data[3] = (uint64_t)ap_entry;

    DEBUG("SMP: AP data at 0x%x: CR3=0x%llx, stack=0x%llx, percpu=0x%llx",
          AP_TRAMPOLINE_DATA, data[0], data[1], data[2]);
}

/*
 * Boot a single AP
 */
static bool boot_ap(uint32_t cpu_id, uint8_t apic_id)
{
    INFO("SMP: Booting CPU %u (APIC ID %u)...", cpu_id, apic_id);

    /* Allocate per-CPU data for this AP */
    percpu_data_t *percpu = percpu_alloc_ap(cpu_id, apic_id);
    if (!percpu) {
        ERROR("SMP: Failed to allocate per-CPU data for CPU %u", cpu_id);
        return false;
    }

    DEBUG("SMP: Per-CPU data for CPU %u at 0x%llx", cpu_id, (unsigned long long)(uint64_t)percpu);

    /* Set up startup data */
    setup_ap_data(percpu);

    DEBUG("SMP: Sending INIT IPI to APIC %u", apic_id);

    /* Send INIT IPI */
    lapic_send_init(apic_id);

    /* Wait 10ms */
    smp_delay_ms(10);

    /* Send INIT de-assert */
    lapic_send_init_deassert();

    DEBUG("SMP: Sending first SIPI to APIC %u, vector=0x%x", apic_id, AP_TRAMPOLINE_ADDR >> 12);

    /* Send first SIPI */
    lapic_send_sipi(apic_id, AP_TRAMPOLINE_ADDR >> 12);

    /* Wait 200us */
    smp_delay_us(200);

    /* Check if AP is online */
    if (percpu->online) {
        INFO("SMP: CPU %u online after first SIPI", cpu_id);
        return true;
    }

    DEBUG("SMP: Sending second SIPI to APIC %u", apic_id);

    /* Send second SIPI */
    lapic_send_sipi(apic_id, AP_TRAMPOLINE_ADDR >> 12);

    /* Wait up to 100ms for AP to come online */
    for (int i = 0; i < 100; i++) {
        smp_delay_ms(1);
        if (percpu->online) {
            INFO("SMP: CPU %u online after second SIPI", cpu_id);
            return true;
        }
    }

    ERROR("SMP: CPU %u (APIC %u) failed to start after 100ms", cpu_id, apic_id);
    return false;
}

/*
 * IPI handler: Reschedule
 */
static void ipi_reschedule_handler(void *cpu_state)
{
    (void)cpu_state;

    /* Acknowledge the IPI */
    lapic_eoi();

    /* Trigger reschedule if scheduler is running */
    if (sched_is_running()) {
        sched_reschedule();
    }
}

/*
 * IPI handler: TLB shootdown
 * Implemented in tlb.c
 */
extern void tlb_shootdown_handler(void *cpu_state);

/*
 * IPI handler: Halt CPU
 */
static void ipi_halt_handler(void *cpu_state)
{
    (void)cpu_state;

    /* Acknowledge the IPI */
    lapic_eoi();

    INFO("SMP: CPU %u halting", cpu_id());

    /* Disable interrupts and halt */
    cli();
    while (1) {
        hlt();
    }
}

/*
 * Register IPI handlers
 */
void smp_register_ipi_handlers(void)
{
    idt_register_handler(IPI_VECTOR_RESCHEDULE, ipi_reschedule_handler);
    idt_register_handler(IPI_VECTOR_HALT, ipi_halt_handler);

    /* TLB handler is registered separately in tlb.c */

    DEBUG("SMP: IPI handlers registered (reschedule=%u, halt=%u)",
          IPI_VECTOR_RESCHEDULE, IPI_VECTOR_HALT);
}

/*
 * AP entry point - called from ap_trampoline.asm
 */
void ap_entry(percpu_data_t *percpu)
{
    /* Set up GS_BASE for per-CPU access */
    write_msr(MSR_GS_BASE, (uint64_t)percpu);

    /* Initialize per-CPU GDT/TSS */
    gdt_init_cpu(percpu);

    /* Load the shared IDT */
    extern void idt_load(void);
    idt_load();

    /* Initialize Local APIC for this AP */
    lapic_init_ap();

    /* Initialize per-CPU scheduler */
    sched_init_cpu(percpu);

    /* Mark this CPU as online */
    percpu->online = true;

    /* Increment online CPU counter */
    extern void percpu_cpu_online(void);
    percpu_cpu_online();

    __atomic_fetch_add(&aps_booted, 1, __ATOMIC_SEQ_CST);

    INFO("SMP: CPU %u (APIC %u) online", percpu->cpu_id, percpu->apic_id);

    /* Wait for BSP to signal we can continue */
    while (!ap_can_continue) {
        cpu_pause();
    }

    /* Enable interrupts */
    sti();

    /* Enter the idle loop - scheduler will take over */
    DEBUG("SMP: CPU %u entering idle loop", percpu->cpu_id);
    while (1) {
        hlt();
    }
}

/*
 * Initialize SMP subsystem
 */
void smp_init(void)
{
    const acpi_info_t *acpi = acpi_get_info();

    INFO("SMP: Initializing SMP subsystem...");

    if (!acpi || !acpi->valid) {
        WARN("SMP: ACPI not available, assuming single CPU");
        total_cpu_count = 1;
        return;
    }

    total_cpu_count = acpi->cpu_count;
    INFO("SMP: %u CPU(s) detected via ACPI", total_cpu_count);

    if (total_cpu_count <= 1) {
        INFO("SMP: Single CPU system, no APs to boot");
        return;
    }

    /* Register IPI handlers */
    smp_register_ipi_handlers();

    /* Copy trampoline code to low memory */
    copy_trampoline();

    /* Boot each AP */
    uint8_t bsp_apic_id = lapic_get_id();
    uint32_t cpu_id_counter = 1;

    for (uint32_t i = 0; i < acpi->cpu_count; i++) {
        const acpi_cpu_info_t *cpu = &acpi->cpus[i];

        /* Skip BSP */
        if (cpu->apic_id == bsp_apic_id) {
            continue;
        }

        /* Skip disabled CPUs */
        if (!cpu->enabled && !cpu->online_capable) {
            DEBUG("SMP: Skipping disabled CPU (APIC %u)", cpu->apic_id);
            continue;
        }

        /* Boot this AP */
        if (boot_ap(cpu_id_counter, cpu->apic_id)) {
            cpu_id_counter++;
        }
    }

    INFO("SMP: %u AP(s) booted successfully", aps_booted);
}

/*
 * Wait for all APs to come online
 */
uint32_t smp_wait_for_aps(void)
{
    /* Allow APs to continue their initialization */
    ap_can_continue = true;

    /* Mark SMP as initialized */
    extern void percpu_set_smp_init_done(void);
    percpu_set_smp_init_done();

    uint32_t online = cpu_count();
    INFO("SMP: %u CPU(s) online", online);

    return online;
}

/*
 * Get the total number of CPUs detected
 */
uint32_t smp_get_cpu_count(void)
{
    return total_cpu_count;
}

/*
 * Check if SMP is available
 */
bool smp_available(void)
{
    return total_cpu_count > 1;
}

#ifdef DEBUG_TESTS
/*
 * Run SMP tests
 */
void smp_run_tests(void)
{
    kprintf("\n=== SMP Tests ===\n");

    /* Test 1: CPU count */
    kprintf("  Test 1 - CPU count: %u detected, %u online %s\n",
            total_cpu_count, cpu_count(),
            cpu_count() >= 1 ? "OK" : "FAIL");

    /* Test 2: Per-CPU data access */
    percpu_data_t *percpu = percpu_get();
    kprintf("  Test 2 - Per-CPU data: CPU %u, APIC %u, BSP=%d %s\n",
            percpu->cpu_id, percpu->apic_id, percpu->is_bsp,
            (percpu->cpu_id == 0 && percpu->is_bsp) ? "OK" : "FAIL");

    /* Test 3: Self-pointer */
    kprintf("  Test 3 - Self-pointer: %s\n",
            (percpu->self == percpu) ? "OK" : "FAIL");

    /* Test 4: cpu_id() function */
    kprintf("  Test 4 - cpu_id(): %u %s\n",
            cpu_id(), (cpu_id() == 0) ? "OK" : "FAIL");

    /* Test 5: SMP availability */
    kprintf("  Test 5 - SMP available: %s (expected based on ACPI)\n",
            smp_available() ? "yes" : "no");

    kprintf("\n  Summary:\n");
    kprintf("    Total CPUs: %u\n", total_cpu_count);
    kprintf("    Online CPUs: %u\n", cpu_count());
    kprintf("    APs booted: %u\n", aps_booted);
    kprintf("\n");
}
#endif /* DEBUG_TESTS */
