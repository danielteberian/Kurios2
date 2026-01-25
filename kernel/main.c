/* main.c - Kurios2 Kernel Main */

#include "include/types.h"
#include "debug/debug.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/serial.h"
#include "boot_info.h"

#ifdef DEBUG_TESTS
/* Test global constructor */
static void test_constructor(void) {
    /* Note: debug_init() hasn't been called yet, so use serial directly */
    serial_init_default();
    debug_puts("[CTOR] Global constructor called!\r\n");
}

/* Register constructor with priority 101 (runs early) */
__attribute__((section(".init_array"), used))
static void (*_test_ctor)(void) = test_constructor;
#endif /* DEBUG_TESTS */

/* Linker-provided symbols */
extern uint64_t _kernel_start;
extern uint64_t _kernel_end;
extern uint64_t _kernel_phys_start;
extern uint64_t _kernel_phys_end;
extern uint64_t _bss_start;
extern uint64_t _bss_end;
extern uint64_t _kernel_virt_base;
extern uint64_t _kernel_phys_base;

/* Memory type names */
static const char *mmap_type_name(uint32_t type) {
    switch (type) {
        case MMAP_TYPE_USABLE:       return "Usable";
        case MMAP_TYPE_RESERVED:     return "Reserved";
        case MMAP_TYPE_ACPI_RECLAIM: return "ACPI Reclaim";
        case MMAP_TYPE_ACPI_NVS:     return "ACPI NVS";
        case MMAP_TYPE_BAD:          return "Bad Memory";
        default:                     return "Unknown";
    }
}

/* Calculate total usable memory */
static uint64_t calculate_usable_memory(BootInfo *boot_info) UNUSED;
static uint64_t calculate_usable_memory(BootInfo *boot_info) {
    uint64_t total = 0;
    MemoryMapEntry *mmap = (MemoryMapEntry *)(uintptr_t)boot_info->memory_map;

    for (uint64_t i = 0; i < boot_info->memory_count; i++) {
        if (mmap[i].type == MMAP_TYPE_USABLE) {
            total += mmap[i].length;
        }
    }

    return total;
}

/* Kernel main entry point */
void kernel_main(BootInfo *boot_info) {
    /* Initialize debug subsystem first */
    debug_init();

    kprintf("\n");
    INFO("Kurios2 Kernel Starting...");
    kprintf("\n");

    /* Verify boot info */
    if (boot_info == NULL) {
        panic("Boot info pointer is NULL!");
    }

    if (boot_info->magic != KURIOS_BOOT_MAGIC) {
        panic("Invalid boot info magic: 0x%llx (expected 0x%llx)",
              boot_info->magic, KURIOS_BOOT_MAGIC);
    }

    INFO("Boot info validated (magic: 0x%llx)", boot_info->magic);

    /* Print boot information */
    kprintf("\n=== Boot Information ===\n");
    kprintf("  Protocol version: %llu\n", boot_info->version);
    kprintf("  Boot flags:       0x%llx", boot_info->flags);
    if (boot_info->flags & BOOT_FLAG_BIOS) kprintf(" [BIOS]");
    if (boot_info->flags & BOOT_FLAG_UEFI) kprintf(" [UEFI]");
    if (boot_info->flags & BOOT_FLAG_FRAMEBUFFER) kprintf(" [FB]");
    if (boot_info->flags & BOOT_FLAG_ACPI) kprintf(" [ACPI]");
    kprintf("\n");

    /* Print kernel location */
    kprintf("\n=== Kernel Location ===\n");
    kprintf("  Virtual base:  0x%016llx\n", (uint64_t)&_kernel_virt_base);
    kprintf("  Physical base: 0x%016llx\n", boot_info->kernel_phys);
    kprintf("  Kernel start:  0x%016llx\n", (uint64_t)&_kernel_start);
    kprintf("  Kernel end:    0x%016llx\n", (uint64_t)&_kernel_end);
    kprintf("  BSS start:     0x%016llx\n", (uint64_t)&_bss_start);
    kprintf("  BSS end:       0x%016llx\n", (uint64_t)&_bss_end);
    kprintf("  Kernel size:   %llu bytes\n", boot_info->kernel_size);

    /* Print current CPU state */
    kprintf("\n=== CPU State ===\n");
    kprintf("  CR0: 0x%016llx\n", read_cr0());
    kprintf("  CR3: 0x%016llx\n", read_cr3());
    kprintf("  CR4: 0x%016llx\n", read_cr4());

    /* Check if we're really in higher half */
    uint64_t rip;
    __asm__ volatile("lea (%%rip), %0" : "=r"(rip));
    kprintf("  RIP: 0x%016llx\n", rip);

    if (rip >= 0xFFFFFFFF80000000ULL) {
        INFO("Running in higher half - SUCCESS!");
    } else {
        WARN("Not running in higher half! RIP=0x%016llx", rip);
    }

    /* Print memory map */
    kprintf("\n=== Memory Map (%llu entries) ===\n", boot_info->memory_count);

    MemoryMapEntry *mmap = (MemoryMapEntry *)(uintptr_t)boot_info->memory_map;
    uint64_t total_usable = 0;

    for (uint64_t i = 0; i < boot_info->memory_count; i++) {
        uint64_t start = mmap[i].base;
        uint64_t end = start + mmap[i].length;
        uint64_t size_mb = mmap[i].length / (1024 * 1024);

        kprintf("  %016llx - %016llx  %4lluMB  [%s]\n",
                start, end, size_mb, mmap_type_name(mmap[i].type));

        if (mmap[i].type == MMAP_TYPE_USABLE) {
            total_usable += mmap[i].length;
        }
    }

    kprintf("\n  Total usable memory: %llu MB (%llu bytes)\n",
            total_usable / (1024 * 1024), total_usable);

    /* Print ACPI info if available */
    if (boot_info->flags & BOOT_FLAG_ACPI) {
        kprintf("\n=== ACPI ===\n");
        kprintf("  RSDP address: 0x%016llx\n", boot_info->acpi_rsdp);
    }

    /* Print framebuffer info if available */
    if (boot_info->flags & BOOT_FLAG_FRAMEBUFFER) {
        FramebufferInfo *fb = (FramebufferInfo *)(uintptr_t)boot_info->framebuffer;
        kprintf("\n=== Framebuffer ===\n");
        kprintf("  Address:    0x%016llx\n", fb->address);
        kprintf("  Resolution: %ux%u\n", fb->width, fb->height);
        kprintf("  Pitch:      %u bytes/line\n", fb->pitch);
        kprintf("  BPP:        %u\n", fb->bpp);
    }

    /* Assertions test */
    kprintf("\n=== Testing Debug Framework ===\n");

    /* Test logging at different levels */
    TRACE("This is a trace message (may not appear depending on log level)");
    DEBUG("This is a debug message");
    INFO("This is an info message");
    WARN("This is a warning message");
    ERROR("This is an error message (non-fatal)");

    /* Test assertion (should pass) */
    ASSERT(1 == 1);
    INFO("Assertion test passed");

    /* Print completion message */
    kprintf("\n");
    kprintf("=============================================\n");
    kprintf("  Kurios2 Kernel Initialized Successfully!\n");
    kprintf("  Higher-half kernel at 0xFFFFFFFF80000000\n");
    kprintf("=============================================\n");
    kprintf("\n");

    INFO("Kernel initialization complete. Halting.");

    /* Halt - in future this will start the scheduler */
    while (1) {
        hlt();
    }
}
