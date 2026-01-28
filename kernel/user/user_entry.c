/* user_entry.c - User Mode Entry Implementation */

#include "user_entry.h"
#include "../debug/debug.h"
#include "../arch/x86_64/gdt.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "lib/string.h"

/*
 * Test user program machine code
 *
 * This is a minimal user-mode program that:
 * 1. Makes a write syscall to print a message
 * 2. Makes an exit syscall
 *
 * The code is position-independent and self-contained.
 */

/*
 * User test program (x86_64 machine code):
 *
 * ; Print "User mode!\n" via write syscall
 *     mov rax, 1              ; SYS_WRITE
 *     mov rdi, 1              ; fd = stdout
 *     lea rsi, [rip+msg]      ; buf = message
 *     mov rdx, 12             ; count = 12
 *     syscall
 *
 * ; Exit with code 42
 *     mov rax, 60             ; SYS_EXIT
 *     mov rdi, 42             ; status = 42
 *     syscall
 *
 * ; Infinite loop (should never reach here)
 * hang:
 *     jmp hang
 *
 * msg:
 *     db "User mode!", 10, 0
 */
static const uint8_t test_user_code[] = {
    /* write syscall */
    0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00,   /* mov rax, 1 (SYS_WRITE) */
    0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00,   /* mov rdi, 1 (stdout) */
    0x48, 0x8d, 0x35, 0x18, 0x00, 0x00, 0x00,   /* lea rsi, [rip+0x18] (msg) */
    0x48, 0xc7, 0xc2, 0x0c, 0x00, 0x00, 0x00,   /* mov rdx, 12 (length) */
    0x0f, 0x05,                                  /* syscall */

    /* exit syscall */
    0x48, 0xc7, 0xc0, 0x3c, 0x00, 0x00, 0x00,   /* mov rax, 60 (SYS_EXIT) */
    0x48, 0xc7, 0xc7, 0x2a, 0x00, 0x00, 0x00,   /* mov rdi, 42 (exit code) */
    0x0f, 0x05,                                  /* syscall */

    /* hang (should never reach) */
    0xeb, 0xfe,                                  /* jmp $ (infinite loop) */

    /* message string at offset 0x32 */
    'U', 's', 'e', 'r', ' ', 'm', 'o', 'd', 'e', '!', '\n', 0
};

/*
 * Enter user mode
 */
void user_enter(user_entry_t *entry) {
    DEBUG("Entering user mode: RIP=0x%llx, RSP=0x%llx",
          entry->rip, entry->rsp);

    /* Switch to the user's address space */
    if (entry->as) {
        as_switch(entry->as);
    }

    /* Set up TSS.RSP0 for when we return to kernel via syscall/interrupt */
    /* TODO: Set per-process kernel stack in TSS */

    /* Jump to ring 3 via IRETQ */
    ring3_enter(
        entry->rip,
        GDT_USER_CODE_RPL3,     /* CS = 0x23 (user code, RPL 3) */
        entry->rflags,
        entry->rsp,
        GDT_USER_DATA_RPL3      /* SS = 0x1B (user data, RPL 3) */
    );

    /* Never reached */
    __builtin_unreachable();
}

/*
 * Create a test user program
 */
int user_create_test_program(address_space_t *as, user_entry_t *entry) {
    /* Allocate user code page at USER_SPACE_START */
    uint64_t code_virt = USER_SPACE_START;
    if (as_alloc_page(as, code_virt, PTE_USER_RWX) < 0) {
        ERROR("Failed to allocate user code page");
        return -1;
    }

    /* Allocate user stack (grows down from USER_STACK_TOP) */
    uint64_t stack_top = USER_STACK_TOP;
    uint64_t stack_base = stack_top - USER_STACK_SIZE;

    /* Allocate stack pages */
    for (uint64_t addr = stack_base; addr < stack_top; addr += PAGE_SIZE) {
        if (as_alloc_page(as, addr, PTE_USER_RW) < 0) {
            ERROR("Failed to allocate user stack page at 0x%llx", addr);
            /* TODO: Clean up already allocated pages */
            return -1;
        }
    }

    /*
     * Copy test code to user page
     * We need to write to the physical page since we're in kernel address space
     */
    uint64_t code_phys = as_get_phys(as, code_virt);
    if (code_phys == 0) {
        ERROR("Failed to get physical address for user code");
        return -1;
    }

    /* Map the physical page into kernel space temporarily to copy code */
    uint64_t temp_virt = 0xFFFFFFFF90100000UL;  /* Temporary kernel mapping */
    if (vmm_map_page(temp_virt, code_phys, PTE_KERNEL_RW) < 0) {
        ERROR("Failed to create temporary mapping for code copy");
        return -1;
    }

    /* Copy the test program */
    memcpy((void *)temp_virt, test_user_code, sizeof(test_user_code));

    /* Unmap temporary mapping */
    vmm_unmap_page(temp_virt);

    /* Set up entry point */
    entry->rip = code_virt;
    entry->rsp = stack_top - 8;  /* Stack grows down, align to 8 */
    entry->rflags = USER_RFLAGS_DEFAULT;
    entry->as = as;

    INFO("Test user program created: code=0x%llx, stack=0x%llx",
         entry->rip, entry->rsp);

    return 0;
}

#ifdef DEBUG_TESTS
/*
 * Test user mode entry
 *
 * This test:
 * 1. Creates a new address space
 * 2. Sets up a test user program
 * 3. Enters user mode
 * 4. The user program makes a syscall to write and then exit
 *
 * NOTE: This test does not return! The user program calls exit().
 */
void user_entry_run_tests(void) {
    kprintf("\n=== User Mode Entry Test ===\n");

    /* Create a new address space */
    address_space_t *as = as_create();
    if (!as) {
        kprintf("  FAIL: Could not create address space\n");
        return;
    }
    kprintf("  Created address space (CR3=0x%llx)\n", as->cr3);

    /* Set up test user program */
    user_entry_t entry;
    if (user_create_test_program(as, &entry) < 0) {
        kprintf("  FAIL: Could not create test program\n");
        as_destroy(as);
        return;
    }

    kprintf("  Test program ready:\n");
    kprintf("    Entry point: 0x%llx\n", entry.rip);
    kprintf("    Stack:       0x%llx\n", entry.rsp);
    kprintf("    RFLAGS:      0x%llx\n", entry.rflags);

    kprintf("\n  Entering user mode...\n");
    kprintf("  (Expect 'User mode!' message followed by exit)\n\n");

    /* Enter user mode - this does not return! */
    user_enter(&entry);

    /* Never reached */
    kprintf("  ERROR: user_enter returned!\n");
}
#endif
