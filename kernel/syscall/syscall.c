/* syscall.c - System Call Implementation */

#include "syscall.h"
#include "../debug/debug.h"
#include "../arch/x86_64/gdt.h"
#include "../include/types.h"
#include "../mm/as.h"

/*
 * Model Specific Registers for SYSCALL/SYSRET
 */
#define MSR_EFER        0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR        0xC0000081  /* Segment selectors for SYSCALL/SYSRET */
#define MSR_LSTAR       0xC0000082  /* SYSCALL entry point (64-bit) */
#define MSR_CSTAR       0xC0000083  /* SYSCALL entry point (compat mode, unused) */
#define MSR_SFMASK      0xC0000084  /* RFLAGS mask for SYSCALL */

/* EFER bits */
#define EFER_SCE        (1UL << 0)  /* SYSCALL/SYSRET enable */

/* RFLAGS bits to mask on SYSCALL entry */
#define RFLAGS_IF       (1UL << 9)  /* Interrupt Flag */
#define RFLAGS_TF       (1UL << 8)  /* Trap Flag */
#define RFLAGS_DF       (1UL << 10) /* Direction Flag */
#define RFLAGS_AC       (1UL << 18) /* Alignment Check */

/*
 * Read MSR
 */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/*
 * Write MSR
 */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

/*
 * Syscall handler table
 */
static syscall_handler_t syscall_table[SYS_MAX];
static bool syscall_initialized = false;

/*
 * Default handler for unimplemented syscalls
 */
static int64_t sys_unimplemented(uint64_t arg1, uint64_t arg2,
                                  uint64_t arg3, uint64_t arg4,
                                  uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;
    return -ENOSYS;
}

/*
 * sys_exit - terminate the current process
 */
static int64_t sys_exit(uint64_t status, uint64_t arg2,
                        uint64_t arg3, uint64_t arg4,
                        uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    INFO("sys_exit: Process exiting with status %llu", status);

    /*
     * Switch back to kernel address space
     * This ensures we don't crash when the user's address space is destroyed
     */
    as_switch(as_get_kernel());

    kprintf("\n=== User Process Exited ===\n");
    kprintf("  Exit code: %llu\n", status);
    kprintf("  (Process cleanup not yet implemented)\n");
    kprintf("  System halting.\n");

    /* Disable interrupts and halt */
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }

    return 0;
}

/*
 * sys_write - write to a file descriptor
 */
static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_write(fd=%llu, buf=0x%llx, count=%llu)", fd, buf, count);

    /* TODO: Proper fd handling and user pointer validation */
    /* For now, just output to serial if fd is 1 (stdout) or 2 (stderr) */
    if (fd == 1 || fd == 2) {
        const char *str = (const char *)buf;
        for (uint64_t i = 0; i < count; i++) {
            kprintf("%c", str[i]);
        }
        return (int64_t)count;
    }

    return -EBADF;
}

/*
 * sys_getpid - get current process ID
 */
static int64_t sys_getpid(uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t arg4,
                          uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;

    /* TODO: Return actual PID from process_current() */
    return 0;  /* Kernel process */
}

/*
 * Main syscall dispatcher
 */
int64_t syscall_dispatch(syscall_frame_t *frame) {
    uint64_t syscall_num = frame->rax;

    TRACE("syscall_dispatch: num=%llu, rdi=0x%llx, rsi=0x%llx, rdx=0x%llx",
          syscall_num, frame->rdi, frame->rsi, frame->rdx);

    if (syscall_num >= SYS_MAX) {
        WARN("Invalid syscall number: %llu", syscall_num);
        return -ENOSYS;
    }

    syscall_handler_t handler = syscall_table[syscall_num];
    if (!handler) {
        handler = sys_unimplemented;
    }

    return handler(frame->rdi, frame->rsi, frame->rdx,
                   frame->r10, frame->r8, frame->r9);
}

/*
 * Register a syscall handler
 */
int syscall_register(int num, syscall_handler_t handler) {
    if (num < 0 || num >= SYS_MAX) {
        ERROR("Invalid syscall number: %d", num);
        return -1;
    }

    if (syscall_table[num] != NULL && syscall_table[num] != sys_unimplemented) {
        WARN("Syscall %d already registered", num);
        return -1;
    }

    syscall_table[num] = handler;
    DEBUG("Registered syscall %d", num);
    return 0;
}

/*
 * Initialize syscall infrastructure
 */
void syscall_init(void) {
    INFO("Initializing syscall infrastructure...");

    /* Explicitly initialize static variables in case static init failed */
    syscall_initialized = false;

    /* Initialize syscall table with unimplemented handlers */
    for (int i = 0; i < SYS_MAX; i++) {
        syscall_table[i] = NULL;
    }

    /* Register basic syscalls */
    syscall_register(SYS_EXIT, sys_exit);
    syscall_register(SYS_WRITE, sys_write);
    syscall_register(SYS_GETPID, sys_getpid);

    /*
     * Configure MSRs for SYSCALL/SYSRET
     *
     * STAR MSR format:
     *   [63:48] = SYSRET CS and SS (CS = value + 16, SS = value + 8, both with RPL 3)
     *   [47:32] = SYSCALL CS and SS (CS = value, SS = value + 8)
     *   [31:0]  = Reserved (target EIP for 32-bit SYSCALL, unused in 64-bit)
     *
     * With our GDT layout:
     *   Kernel CS = 0x08, Kernel SS = 0x10
     *   User CS = 0x20 (+ RPL 3 = 0x23), User SS = 0x18 (+ RPL 3 = 0x1B)
     *
     * So STAR = ((0x10UL << 48) | (0x08UL << 32))
     *   SYSCALL: CS = 0x08, SS = 0x10
     *   SYSRET:  CS = 0x10 + 16 = 0x20 (+ RPL 3), SS = 0x10 + 8 = 0x18 (+ RPL 3)
     */
    uint64_t star = ((uint64_t)GDT_KERNEL_DATA << 48) |  /* SYSRET base (0x10) */
                    ((uint64_t)GDT_KERNEL_CODE << 32);   /* SYSCALL CS (0x08) */

    wrmsr(MSR_STAR, star);
    DEBUG("MSR_STAR = 0x%016llx", star);

    /* Set LSTAR to our syscall entry point */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    DEBUG("MSR_LSTAR = 0x%016llx (syscall_entry)", (uint64_t)syscall_entry);

    /* Set CSTAR (unused in 64-bit mode, but clear it anyway) */
    wrmsr(MSR_CSTAR, 0);

    /*
     * SFMASK: RFLAGS bits to clear on SYSCALL entry
     * We clear: IF (disable interrupts), TF (no single-stepping),
     *           DF (clear direction flag), AC (no alignment check)
     */
    uint64_t sfmask = RFLAGS_IF | RFLAGS_TF | RFLAGS_DF | RFLAGS_AC;
    wrmsr(MSR_SFMASK, sfmask);
    DEBUG("MSR_SFMASK = 0x%016llx", sfmask);

    /* Enable SYSCALL/SYSRET in EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);
    DEBUG("MSR_EFER = 0x%016llx (SCE enabled)", efer);

    syscall_initialized = true;
    INFO("Syscall infrastructure initialized");
}

#ifdef DEBUG_TESTS
/*
 * Test syscall infrastructure (kernel-side only, no actual SYSCALL instruction)
 */
void syscall_run_tests(void) {
    kprintf("\n=== Syscall Infrastructure Tests ===\n");

    /* Test 1: Verify initialization */
    kprintf("  Test 1 - Initialized: %s\n",
            syscall_initialized ? "OK" : "FAIL");

    /* Test 2: Verify EFER.SCE is set */
    uint64_t efer = rdmsr(MSR_EFER);
    kprintf("  Test 2 - EFER.SCE enabled: %s (EFER=0x%llx)\n",
            (efer & EFER_SCE) ? "OK" : "FAIL", efer);

    /* Test 3: Verify LSTAR points to entry */
    uint64_t lstar = rdmsr(MSR_LSTAR);
    kprintf("  Test 3 - LSTAR set: %s (0x%llx)\n",
            (lstar == (uint64_t)syscall_entry) ? "OK" : "FAIL", lstar);

    /* Test 4: Verify STAR is correct */
    uint64_t star = rdmsr(MSR_STAR);
    uint64_t expected_star = ((uint64_t)GDT_KERNEL_DATA << 48) |
                             ((uint64_t)GDT_KERNEL_CODE << 32);
    kprintf("  Test 4 - STAR correct: %s (0x%llx)\n",
            (star == expected_star) ? "OK" : "FAIL", star);

    /* Test 5: Test dispatcher with fake frame */
    syscall_frame_t fake_frame = {0};
    fake_frame.rax = SYS_GETPID;
    int64_t result = syscall_dispatch(&fake_frame);
    kprintf("  Test 5 - Dispatch getpid: %s (result=%lld)\n",
            (result == 0) ? "OK" : "FAIL", (long long)result);

    /* Test 6: Test invalid syscall */
    fake_frame.rax = 999;
    result = syscall_dispatch(&fake_frame);
    kprintf("  Test 6 - Invalid syscall: %s (result=%lld)\n",
            (result == -ENOSYS) ? "OK" : "FAIL", (long long)result);

    /* Test 7: Test write syscall */
    const char *test_msg = "[syscall test]";
    fake_frame.rax = SYS_WRITE;
    fake_frame.rdi = 1;  /* stdout */
    fake_frame.rsi = (uint64_t)test_msg;
    fake_frame.rdx = 14;
    result = syscall_dispatch(&fake_frame);
    kprintf("\n  Test 7 - Write syscall: %s (wrote %lld bytes)\n",
            (result == 14) ? "OK" : "FAIL", (long long)result);

    kprintf("\n  Syscall infrastructure tests complete.\n");
}
#endif
