/* main.c - Kurios2 Kernel Main */

#include "include/types.h"
#include "debug/debug.h"
#include "stack_protector.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/slab.h"

#ifdef TEST_MODE
#include "tests/tests.h"
#endif
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"
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

/*
 * Test stack smashing detection.
 * This function intentionally corrupts its stack canary.
 * Only enable this to test that stack protection works!
 */
__attribute__((noinline, optimize("O0")))
static void test_stack_smash(void) {
    char buf[16];
    volatile char *p = buf;
    /* Overflow the buffer to corrupt the stack canary */
    for (volatile int i = 0; i < 64; i++) {
        p[i] = 'A';
    }
    /* If stack protection works, we should never reach here */
    kprintf("ERROR: Stack smash was not detected!\n");
}
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

    /* Initialize stack protector with randomized canary */
    stack_protector_init();

    /* Initialize GDT and TSS */
    gdt_init();

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

    /* Initialize physical memory manager */
    pmm_init(boot_info);
    pmm_dump_stats();
    pmm_dump_free_lists();

    /* Test PMM allocation and freeing */
    kprintf("\n=== PMM Allocation Tests ===\n");

    /* Test 1: Allocate single page */
    uint64_t page1 = alloc_page();
    kprintf("  Test 1 - alloc_page(): 0x%llx %s\n", page1, page1 ? "OK" : "FAIL");

    /* Test 2: Allocate another single page (should be different) */
    uint64_t page2 = alloc_page();
    kprintf("  Test 2 - alloc_page(): 0x%llx %s\n", page2,
            (page2 && page2 != page1) ? "OK" : "FAIL");

    /* Test 3: Allocate order-2 (4 pages = 16KB) */
    uint64_t pages4 = alloc_pages(2);
    kprintf("  Test 3 - alloc_pages(2): 0x%llx %s\n", pages4, pages4 ? "OK" : "FAIL");

    /* Test 4: Allocate order-10 (1024 pages = 4MB) */
    uint64_t pages4mb = alloc_pages(10);
    kprintf("  Test 4 - alloc_pages(10): 0x%llx %s\n", pages4mb, pages4mb ? "OK" : "FAIL");

    /* Show stats after allocation */
    kprintf("\n  After allocations:\n");
    kprintf("    Free pages: %llu\n", mem_info.free_pages);

    /* Test 5: Free the pages */
    kprintf("\n  Freeing allocations...\n");
    free_page(page1);
    free_page(page2);
    free_pages(pages4, 2);
    free_pages(pages4mb, 10);

    kprintf("    Free pages after free: %llu\n", mem_info.free_pages);

    /* Test 6: Allocate and free in a loop to test buddy merging */
    kprintf("\n  Test 6 - Buddy merging test:\n");
    uint64_t addrs[8];
    for (int i = 0; i < 8; i++) {
        addrs[i] = alloc_pages(7);  /* 512KB each */
    }
    kprintf("    Allocated 8 x 512KB blocks\n");
    kprintf("    Free pages: %llu\n", mem_info.free_pages);

    /* Free in reverse order to test merging */
    for (int i = 7; i >= 0; i--) {
        free_pages(addrs[i], 7);
    }
    kprintf("    Freed all blocks\n");
    kprintf("    Free pages after merge: %llu\n", mem_info.free_pages);

    /* Final stats */
    kprintf("\n=== PMM After Tests ===\n");
    pmm_dump_free_lists();

    /* Initialize virtual memory manager */
    vmm_init();

    /* Test VMM */
    kprintf("\n=== VMM Tests ===\n");

    /* Test 1: Check existing kernel mapping */
    uint64_t kernel_virt = 0xFFFFFFFF80000000UL;
    uint64_t kernel_phys = vmm_get_phys(kernel_virt);
    kprintf("  Test 1 - Kernel mapping: virt 0x%llx -> phys 0x%llx %s\n",
            kernel_virt, kernel_phys,
            (kernel_phys == 0x200000) ? "OK" : "FAIL");

    /* Test 2: Map a new page */
    uint64_t test_phys = alloc_page();
    uint64_t test_virt = 0xFFFFFFFF90000000UL;  /* Unused kernel space */

    int map_result = vmm_map_page(test_virt, test_phys, PTE_KERNEL_RW);
    kprintf("  Test 2 - Map new page: %s\n", (map_result == 0) ? "OK" : "FAIL");

    /* Test 3: Verify the mapping */
    uint64_t verify_phys = vmm_get_phys(test_virt);
    kprintf("  Test 3 - Verify mapping: 0x%llx -> 0x%llx %s\n",
            test_virt, verify_phys,
            (verify_phys == test_phys) ? "OK" : "FAIL");

    /* Test 4: Write to the mapped page */
    volatile uint64_t *ptr = (volatile uint64_t *)test_virt;
    *ptr = 0xDEADBEEFCAFEBABEULL;
    uint64_t read_back = *ptr;
    kprintf("  Test 4 - Write/read: 0x%llx %s\n",
            read_back,
            (read_back == 0xDEADBEEFCAFEBABEULL) ? "OK" : "FAIL");

    /* Test 5: Unmap the page */
    uint64_t unmapped = vmm_unmap_page(test_virt);
    kprintf("  Test 5 - Unmap: returned 0x%llx %s\n",
            unmapped,
            (unmapped == test_phys) ? "OK" : "FAIL");

    /* Test 6: Verify unmapped */
    bool still_mapped = vmm_is_mapped(test_virt);
    kprintf("  Test 6 - Verify unmapped: %s\n",
            (!still_mapped) ? "OK" : "FAIL");

    /* Free the test page */
    free_page(test_phys);

    /* Test 7: Map multiple pages */
    uint64_t multi_phys = alloc_pages(2);  /* 4 pages */
    uint64_t multi_virt = 0xFFFFFFFF90001000UL;
    map_result = vmm_map_pages(multi_virt, multi_phys, 4, PTE_KERNEL_RW);
    kprintf("  Test 7 - Map 4 pages: %s\n", (map_result == 0) ? "OK" : "FAIL");

    /* Verify all 4 pages */
    bool all_ok = true;
    for (int i = 0; i < 4; i++) {
        if (vmm_get_phys(multi_virt + i * 0x1000) != multi_phys + i * 0x1000) {
            all_ok = false;
        }
    }
    kprintf("  Test 8 - Verify 4 pages: %s\n", all_ok ? "OK" : "FAIL");

    /* Unmap and free */
    vmm_unmap_pages(multi_virt, 4);
    free_pages(multi_phys, 2);

    /* Dump a PTE for debug */
    kprintf("\n");
    vmm_dump_pte(0xFFFFFFFF80000000UL);

    /* Initialize slab allocator (kernel heap) */
    slab_init();

#ifdef TEST_MODE
    /* Run all kernel tests */
    run_all_tests();

    /* After tests, dump slab stats and halt */
    slab_dump_stats();

    kprintf("\n");
    kprintf("============================================================\n");
    kprintf("  Test run complete. System halting.\n");
    kprintf("============================================================\n");

    while (1) {
        hlt();
    }
#endif

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

#ifdef DEBUG_TESTS
    /*
     * Uncomment to test stack smash detection (will panic):
     * INFO("Testing stack smash detection...");
     * test_stack_smash();
     */
#endif

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
