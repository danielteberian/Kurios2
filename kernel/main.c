/* main.c - Kurios2 Kernel Main */

#include "include/types.h"
#include "debug/debug.h"
#include "stack_protector.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/slab.h"
#include "mm/as.h"
#include "mm/vma.h"
#include "mm/fault.h"

#ifdef TEST_MODE
#include "tests/tests.h"
#endif
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/serial.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/vga.h"
#include "drivers/tty.h"
#include "drivers/pit.h"
#include "sched/thread.h"
#include "sched/sched.h"
#include "process/process.h"
#include "syscall/syscall.h"
#include "user/user_entry.h"
#include "loader/elf_loader.h"
#include "debug/gdb_stub.h"
#include "fs/vfs.h"
#include "fs/ramfs.h"
#include "fs/procfs.h"
#include "lib/string.h"
#include "boot_info.h"
#include "acpi/acpi.h"
#include "apic/apic.h"
#include "drivers/hpet.h"
#include "drivers/pci.h"
#include "drivers/block.h"
#include "drivers/virtio/virtio.h"
#include "initrd/initrd.h"
#include "smp/percpu.h"
#include "smp/smp.h"
#include "smp/tlb.h"
#include "signal/signal.h"

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
__attribute__((noinline, optimize("O0"), unused))
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

/*
 * Demo thread function - prints messages periodically
 */
static void demo_thread(void *arg) {
    int id = (int)(uint64_t)arg;
    int count = 0;

    while (count < 5) {
        kprintf("[Thread %d] count=%d\n", id, count);
        count++;
        thread_sleep_ms(500);  /* Sleep 500ms */
    }

    kprintf("[Thread %d] exiting\n", id);
}

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

    /* Initialize IDT and exception handlers */
    idt_init();

    /* Initialize page fault handler (COW, demand paging) */
    fault_init();

    /* Initialize GDB stub for remote debugging */
    gdb_init();

#ifdef GDB_BREAK_ON_BOOT
    /* Break into debugger early - enable with -DGDB_BREAK_ON_BOOT */
    INFO("Waiting for GDB connection on COM2 (localhost:1234)...");
    gdb_breakpoint();
#endif

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
    if (boot_info->flags & BOOT_FLAG_INITRD) kprintf(" [INITRD]");
    kprintf("\n");
    kprintf("  Initrd start:   0x%llx\n", boot_info->initrd_start);
    kprintf("  Initrd size:    %llu bytes\n", boot_info->initrd_size);
    /* Debug: raw bytes at boot_info+0x58 */
    uint8_t *raw = (uint8_t *)boot_info;
    kprintf("  Raw at 0x58:    %02x %02x %02x %02x %02x %02x %02x %02x\n",
            raw[0x58], raw[0x59], raw[0x5a], raw[0x5b],
            raw[0x5c], raw[0x5d], raw[0x5e], raw[0x5f]);

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

    /* Initialize VMA subsystem (for demand paging) */
    vma_init();

    /* Initialize ACPI table parsing */
    if (acpi_init(boot_info) != 0) {
        WARN("ACPI initialization failed - APIC info not available");
    }

    /* Initialize APIC (requires ACPI for addresses) */
    if (apic_init() != 0) {
        WARN("APIC initialization failed - using legacy PIC");
    }

    /* Initialize HPET (requires ACPI for address) */
    if (hpet_init() != 0) {
        DEBUG("HPET not available - using PIT for timing");
    }

    /* Initialize LAPIC timer (uses HPET for calibration) */
    lapic_timer_init();

    /* Initialize per-CPU data for BSP (before SMP) */
    percpu_init_bsp();

    /* Initialize SMP - boot Application Processors */
    smp_init();

    /* Initialize TLB shootdown subsystem */
    tlb_init();

    /* Wait for all APs to come online */
    uint32_t online_cpus = smp_wait_for_aps();
    INFO("SMP: %u CPU(s) online", online_cpus);

#ifdef DEBUG_TESTS
    /* Run ACPI tests */
    acpi_run_tests();

    /* Run APIC tests */
    apic_run_tests();

    /* Run HPET tests */
    hpet_run_tests();

    /* Run SMP tests */
    smp_run_tests();

    /* Run address space tests (requires slab allocator) */
    as_run_tests();
#endif

    /* Initialize initrd (if present) - must be before VFS for mounting */
    initrd_init(boot_info);

    /* Initialize VFS and ramfs */
    vfs_init();
    ramfs_init();

    /* Mount ramfs at root */
    if (vfs_mount(NULL, "/", "ramfs", 0) == VFS_OK) {
        INFO("Mounted ramfs at /");
    } else {
        ERROR("Failed to mount ramfs at /");
    }

    /* Mount initrd contents to ramfs (if initrd is present) */
    if (initrd_available()) {
        if (initrd_mount() == 0) {
            INFO("Initrd contents mounted to ramfs");
        } else {
            WARN("Failed to mount initrd contents");
        }
    }

    /* Mount procfs at /proc */
    if (procfs_mount() == VFS_OK) {
        INFO("Mounted procfs at /proc");
    } else {
        WARN("Failed to mount procfs");
    }

#ifdef DEBUG_TESTS
    /* Run initrd tests */
    initrd_run_tests();

    /* Run procfs tests */
    procfs_run_tests();
#endif

    /* VFS Tests */
    kprintf("\n=== VFS Tests ===\n");

    /* Test 1: Create directories */
    int err = vfs_mkdir("/tmp");
    kprintf("  Test 1 - mkdir /tmp: %s\n", err == VFS_OK ? "OK" : "FAIL");

    err = vfs_mkdir("/home");
    kprintf("  Test 2 - mkdir /home: %s\n", err == VFS_OK ? "OK" : "FAIL");

    /* Test 3: Create and write a file */
    int fd = vfs_open("/tmp/test.txt", O_CREAT | O_RDWR);
    kprintf("  Test 3 - create /tmp/test.txt: %s (fd=%d)\n",
            fd >= 0 ? "OK" : "FAIL", fd);

    if (fd >= 0) {
        /* Test 4: Write to file */
        const char *msg = "Hello, VFS!\nThis is a test file.\n";
        ssize_t written = vfs_write(fd, msg, strlen(msg));
        kprintf("  Test 4 - write %lld bytes: %s\n", (long long)written,
                written == (ssize_t)strlen(msg) ? "OK" : "FAIL");

        /* Test 5: Seek to beginning */
        int64_t pos = vfs_seek(fd, 0, SEEK_SET);
        kprintf("  Test 5 - seek to 0: %s\n", pos == 0 ? "OK" : "FAIL");

        /* Test 6: Read back */
        char buf[128];
        ssize_t bytes_read = vfs_read(fd, buf, sizeof(buf) - 1);
        if (bytes_read > 0) {
            buf[bytes_read] = '\0';
            kprintf("  Test 6 - read %lld bytes: %s\n", (long long)bytes_read,
                    bytes_read == written ? "OK" : "FAIL");
            kprintf("    Content: \"%s\"\n", buf);
        } else {
            kprintf("  Test 6 - read: FAIL\n");
        }

        vfs_close(fd);
    }

    /* Test 7: Create another file */
    fd = vfs_open("/tmp/hello.txt", O_CREAT | O_WRONLY);
    if (fd >= 0) {
        vfs_write(fd, "Hello World!", 12);
        vfs_close(fd);
        kprintf("  Test 7 - create /tmp/hello.txt: OK\n");
    }

    /* Test 8: Read directory */
    fd = vfs_open("/tmp", O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        kprintf("  Test 8 - readdir /tmp:\n");
        dirent_t dent;
        while (vfs_readdir(fd, &dent) == VFS_OK) {
            kprintf("    - %s (%s)\n", dent.name,
                    dent.type == VFS_DIR ? "dir" : "file");
        }
        vfs_close(fd);
    }

    /* Test 9: File stat */
    vfs_stat_t st;
    err = vfs_stat("/tmp/test.txt", &st);
    kprintf("  Test 9 - stat /tmp/test.txt: %s (size=%llu)\n",
            err == VFS_OK ? "OK" : "FAIL", st.size);

    /* Test 10: Unlink file */
    err = vfs_unlink("/tmp/hello.txt");
    kprintf("  Test 10 - unlink /tmp/hello.txt: %s\n",
            err == VFS_OK ? "OK" : "FAIL");

    /* Show VFS tree */
    kprintf("\n=== VFS Tree ===\n");
    vfs_dump_tree(NULL, 0);

    /* Initialize keyboard driver */
    keyboard_init();

    /* Initialize PS/2 mouse driver */
    mouse_init();

    /* Initialize VGA text mode */
    vga_init();

    /* Initialize TTY (/dev/console) */
    tty_init();

    /* Initialize PCI subsystem */
    pci_init();

    /* Initialize block device layer */
    block_init();

    /* Initialize virtio devices (scans PCI for virtio-blk, etc.) */
    virtio_init();

    /* Initialize PIT timer at 100 Hz (10ms tick) */
    pit_init(100);

    /* Initialize threading subsystem */
    thread_init();

    /* Initialize process management */
    process_init();

    /* Initialize signal subsystem */
    signal_init();

    /* Initialize syscall infrastructure */
    syscall_init();

#ifdef DEBUG_TESTS
    /* Run process subsystem tests */
    process_run_tests();

    /* Run syscall infrastructure tests */
    syscall_run_tests();

    /* Run ELF loader tests */
    elf_loader_run_tests();
#endif

    /* Create demo threads */
    thread_t *t1 = thread_create("worker1", demo_thread, (void *)1);
    thread_t *t2 = thread_create("worker2", demo_thread, (void *)2);

    if (t1 && t2) {
        INFO("Created demo threads: TID %u, TID %u", t1->tid, t2->tid);
    } else {
        ERROR("Failed to create demo threads");
    }

    /* Start LAPIC timer for per-CPU scheduling (100 Hz) */
    lapic_timer_start(100);

    /* Start the scheduler */
    sched_start();

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

    INFO("Kernel initialization complete.");

    /* Enable interrupts for keyboard */
    sti();

    /* Show prompt on both serial and VGA */
    kprintf("\n");
    kprintf("Keyboard ready. Type something (Ctrl+C to halt):\n> ");

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("Kurios2 Kernel Ready!\n\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("Type something (Ctrl+C to halt):\n> ");

    /* Simple keyboard echo loop */
    while (1) {
        char c = keyboard_getchar();

        if (keyboard_ctrl_pressed() && c == 'c') {
            kprintf("\n\nCtrl+C pressed. Halting.\n");
            vga_puts("\n\nHalting...\n");
            break;
        }

#ifdef DEBUG_TESTS
        /* Ctrl+U: Enter user mode test */
        if (keyboard_ctrl_pressed() && c == 'u') {
            kprintf("\n\nCtrl+U pressed. Running user mode test...\n");
            kprintf("(This will enter user mode and exit via syscall)\n\n");
            vga_puts("\n\nEntering user mode...\n");
            user_entry_run_tests();
            /* Note: user_entry_run_tests() does not return */
        }
#endif

        if (c == '\n') {
            uint64_t uptime = pit_get_uptime_ms();
            kprintf(" [%llu.%03llus]\n> ", uptime / 1000, uptime % 1000);
            vga_puts("\n> ");
        } else if (c == '\b' || c == KEY_DELETE) {
            /* Backspace and Delete both erase */
            kprintf("\b \b");
            vga_putc('\b');
        } else if ((uint8_t)c >= 0x80) {
            /* Ignore other special keys (arrows, etc.) for now */
            continue;
        } else if (c >= 0x20) {
            /* Printable characters */
            kprintf("%c", c);
            vga_putc(c);
        }
    }

    /* Halt */
    cli();
    while (1) {
        hlt();
    }
}
