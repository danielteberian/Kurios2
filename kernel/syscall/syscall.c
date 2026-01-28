/* syscall.c - System Call Implementation */

#include "syscall.h"
#include "../debug/debug.h"
#include "../arch/x86_64/gdt.h"
#include "../include/types.h"
#include "../mm/as.h"
#include "../mm/uaccess.h"
#include "../process/process.h"
#include "../sched/sched.h"
#include "../loader/elf_loader.h"
#include "../fs/vfs.h"
#include "../fs/fd_table.h"
#include "../mm/slab.h"
#include "lib/string.h"

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
 *
 * For stdout/stderr (fd 1, 2): outputs to serial console
 * For other fds: TODO - use VFS
 */
#define WRITE_CHUNK_SIZE 256

static int64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_write(fd=%llu, buf=0x%llx, count=%llu)", fd, buf, count);

    /* Validate user buffer */
    if (count > 0 && !access_ok((const void *)buf, count)) {
        return -EFAULT;
    }

    /* Handle stdout/stderr - output to serial console */
    if (fd == 1 || fd == 2) {
        char kbuf[WRITE_CHUNK_SIZE];
        uint64_t written = 0;

        while (written < count) {
            /* Copy chunk from user space */
            uint64_t chunk = count - written;
            if (chunk > WRITE_CHUNK_SIZE) {
                chunk = WRITE_CHUNK_SIZE;
            }

            int err = copy_from_user(kbuf, (const void *)(buf + written), chunk);
            if (err < 0) {
                return written > 0 ? (int64_t)written : err;
            }

            /* Output to console */
            for (uint64_t i = 0; i < chunk; i++) {
                kprintf("%c", kbuf[i]);
            }

            written += chunk;
        }

        return (int64_t)written;
    }

    /* TODO: Handle other file descriptors via VFS */
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

    process_t *proc = process_current();
    if (proc) {
        return (int64_t)proc->pid;
    }
    return 0;  /* Kernel process */
}

/*
 * sys_getppid - get parent process ID
 */
static int64_t sys_getppid(uint64_t arg1, uint64_t arg2,
                           uint64_t arg3, uint64_t arg4,
                           uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (proc) {
        return (int64_t)proc->parent_pid;
    }
    return 0;
}

/*
 * Fork return assembly entry point (defined in syscall_entry.asm)
 * This is called by the child after fork to return to user mode.
 * Takes pointer to syscall_frame_t in RDI.
 */
extern void fork_child_return(syscall_frame_t *frame) __attribute__((noreturn));

/*
 * Fork child context - passed to fork_child_entry
 */
typedef struct {
    syscall_frame_t frame;      /* Copy of parent's syscall frame */
    process_t *child_proc;      /* Child process pointer */
} fork_child_ctx_t;

/*
 * Child process entry point for fork
 * This function is called when the child process first runs.
 * It sets up the return state and jumps to user mode.
 */
static void fork_child_entry(void *arg) {
    fork_child_ctx_t *ctx = (fork_child_ctx_t *)arg;

    DEBUG("fork_child_entry: rip=0x%llx, rsp=0x%llx, rflags=0x%llx",
          ctx->frame.rcx, ctx->frame.rsp, ctx->frame.r11);

    /* The child returns 0 from fork */
    ctx->frame.rax = 0;

    /*
     * Set this as the current process
     */
    process_set_current(ctx->child_proc);

    /*
     * Switch to the child's address space before returning to user mode.
     */
    if (ctx->child_proc && ctx->child_proc->cr3 != 0) {
        address_space_t child_as;
        child_as.cr3 = ctx->child_proc->cr3;
        child_as.ref_count = 1;
        child_as.user_pages = 0;
        as_switch(&child_as);
        DEBUG("fork_child_entry: switched to child AS cr3=0x%llx",
              ctx->child_proc->cr3);
    }

    /*
     * Return to user mode via assembly helper.
     * This never returns.
     */
    fork_child_return(&ctx->frame);
}

/*
 * sys_fork - create a child process
 *
 * This is handled specially because it needs the full syscall frame.
 * Returns: child PID to parent, 0 to child, -1 on error
 */
static int64_t sys_fork_impl(syscall_frame_t *frame) {
    process_t *parent = process_current();
    if (!parent) {
        ERROR("sys_fork: no current process");
        return -ESRCH;
    }

    INFO("sys_fork: parent PID %u forking", parent->pid);

    /*
     * Step 1: Create child process structure
     */
    char child_name[32];
    int name_len = 0;
    const char *pname = parent->name;
    while (name_len < 24 && pname[name_len]) {
        child_name[name_len] = pname[name_len];
        name_len++;
    }
    child_name[name_len++] = '.';
    child_name[name_len++] = 'c';
    child_name[name_len] = '\0';

    process_t *child = process_create(child_name);
    if (!child) {
        ERROR("sys_fork: failed to create child process");
        return -EAGAIN;
    }

    /*
     * Step 2: Clone parent's address space
     * We need the parent's address space, not the kernel's
     */
    address_space_t parent_as;
    parent_as.cr3 = parent->cr3;
    parent_as.ref_count = 1;
    parent_as.user_pages = 0;  /* Will be counted by clone */

    address_space_t *child_as = as_clone(&parent_as);
    if (!child_as) {
        ERROR("sys_fork: failed to clone address space");
        process_exit(child, -1);
        process_destroy(child);
        return -ENOMEM;
    }

    /* Update child's page table */
    child->cr3 = child_as->cr3;

    /*
     * Step 3: Clone parent's file descriptor table
     * The child was created with an empty fd table - replace it with a clone
     */
    if (parent->fd_table) {
        fd_table_t *child_fdt = fd_table_clone(parent->fd_table);
        if (!child_fdt) {
            ERROR("sys_fork: failed to clone fd table");
            as_destroy(child_as);
            process_exit(child, -1);
            process_destroy(child);
            return -ENOMEM;
        }
        /* Destroy the empty fd table that was created */
        if (child->fd_table) {
            fd_table_destroy(child->fd_table);
        }
        child->fd_table = child_fdt;
    }

    /*
     * Step 4: Allocate fork context for child
     * This structure is placed on the child's kernel stack and contains
     * the syscall frame copy and process pointer.
     */
    fork_child_ctx_t *ctx = (fork_child_ctx_t *)
        (((uint64_t)child->kernel_stack + child->kernel_stack_size) -
         sizeof(fork_child_ctx_t) - 16);  /* 16 bytes alignment */

    /* Copy parent's frame */
    memcpy(&ctx->frame, frame, sizeof(syscall_frame_t));

    /* Child returns 0 */
    ctx->frame.rax = 0;

    /* Store child process pointer */
    ctx->child_proc = child;

    /*
     * Step 4: Create child thread that will "return" from fork
     *
     * The thread entry is fork_child_entry which sets up state and
     * executes SYSRET to return to user mode at the instruction after syscall.
     */
    thread_t *child_thread = thread_create(child_name, fork_child_entry, ctx);
    if (!child_thread) {
        ERROR("sys_fork: failed to create child thread");
        as_destroy(child_as);
        process_exit(child, -1);
        process_destroy(child);
        return -EAGAIN;
    }

    child->main_thread = child_thread;
    child->state = PROC_READY;
    child->entry_point = frame->rcx;  /* User RIP */
    child->user_stack = frame->rsp;   /* User RSP */

    INFO("sys_fork: created child PID %u (parent PID %u)",
         child->pid, parent->pid);

    /* Parent returns child PID */
    return (int64_t)child->pid;
}

/*
 * Maximum file size for exec (1MB should be plenty for now)
 */
#define EXEC_MAX_FILE_SIZE  (1024 * 1024)

/*
 * User stack layout for exec:
 *   [stack_top - 8]     = NULL (end of envp)
 *   [stack_top - 16..]  = envp pointers
 *   [stack_top - N]     = NULL (end of argv)
 *   [stack_top - N-8..] = argv pointers
 *   [stack_top - M]     = argc
 *   Below that          = string data for argv/envp
 *
 * For simplicity, we just set up an empty argv/envp and argc=0
 */
#define USER_STACK_TOP_EXEC 0x7FFFFFF00000UL
#define USER_STACK_PAGES    16  /* 64KB stack */

/*
 * sys_execve - replace current process with new executable
 *
 * @param path  Path to executable (user pointer)
 * @param argv  Argument vector (user pointer, can be NULL)
 * @param envp  Environment vector (user pointer, can be NULL)
 * @return Does not return on success, negative error on failure
 */
/* Maximum path length for execve */
#define EXEC_PATH_MAX 256

static int64_t sys_execve_impl(syscall_frame_t *frame) {
    const char *user_path = (const char *)frame->rdi;
    /* char **argv = (char **)frame->rsi; */  /* TODO: handle argv */
    /* char **envp = (char **)frame->rdx; */  /* TODO: handle envp */

    process_t *proc = process_current();
    if (!proc) {
        ERROR("sys_execve: no current process");
        return -ESRCH;
    }

    /*
     * Copy path from user space
     */
    char path[EXEC_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, user_path, EXEC_PATH_MAX);
    if (path_len < 0) {
        ERROR("sys_execve: invalid path pointer");
        return (int64_t)path_len;
    }

    INFO("sys_execve: PID %u executing '%s'", proc->pid, path);

    /*
     * Step 1: Open and read the executable file
     */
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        ERROR("sys_execve: failed to open '%s': %d", path, fd);
        return -ENOENT;
    }

    /* Get file size */
    vfs_stat_t st;
    if (vfs_fstat(fd, &st) < 0) {
        vfs_close(fd);
        ERROR("sys_execve: failed to stat '%s'", path);
        return -EIO;
    }

    if (st.size > EXEC_MAX_FILE_SIZE) {
        vfs_close(fd);
        ERROR("sys_execve: file too large (%llu bytes)", st.size);
        return -E2BIG;
    }

    if (st.size == 0) {
        vfs_close(fd);
        ERROR("sys_execve: file is empty");
        return -ENOEXEC;
    }

    /* Allocate buffer and read file */
    void *elf_data = kmalloc(st.size);
    if (!elf_data) {
        vfs_close(fd);
        ERROR("sys_execve: failed to allocate %llu bytes", st.size);
        return -ENOMEM;
    }

    ssize_t bytes_read = vfs_read(fd, elf_data, st.size);
    vfs_close(fd);

    if (bytes_read != (ssize_t)st.size) {
        kfree(elf_data);
        ERROR("sys_execve: read error (got %lld, expected %llu)",
              (long long)bytes_read, st.size);
        return -EIO;
    }

    /*
     * Step 2: Create new address space and load ELF
     */
    address_space_t *new_as = as_create();
    if (!new_as) {
        kfree(elf_data);
        ERROR("sys_execve: failed to create address space");
        return -ENOMEM;
    }

    elf_load_result_t elf_result;
    int ret = elf_load(new_as, elf_data, st.size, &elf_result);
    kfree(elf_data);  /* No longer needed */

    if (ret < 0) {
        as_destroy(new_as);
        ERROR("sys_execve: failed to load ELF: %d", ret);
        return -ENOEXEC;
    }

    /*
     * Step 3: Set up user stack
     */
    uint64_t stack_bottom = USER_STACK_TOP_EXEC - (USER_STACK_PAGES * PAGE_SIZE);
    ret = as_alloc_pages(new_as, stack_bottom, USER_STACK_PAGES,
                         PTE_WRITABLE | PTE_USER);
    if (ret < 0) {
        as_destroy(new_as);
        ERROR("sys_execve: failed to allocate user stack");
        return -ENOMEM;
    }

    /*
     * Set up initial stack contents:
     * We need to write to the new address space. Switch temporarily.
     */
    address_space_t *old_as_struct = as_get_kernel();
    uint64_t old_cr3 = proc->cr3;

    /* Switch to new address space to set up stack */
    as_switch(new_as);

    /* Stack pointer starts at top, grows down */
    uint64_t sp = USER_STACK_TOP_EXEC;

    /* Push NULL for envp terminator */
    sp -= 8;
    *(uint64_t *)sp = 0;

    /* Push NULL for argv terminator */
    sp -= 8;
    *(uint64_t *)sp = 0;

    /* Push argc = 0 (no arguments for now) */
    sp -= 8;
    *(uint64_t *)sp = 0;

    /* Switch back temporarily to avoid issues */
    as_switch(old_as_struct);

    /*
     * Step 4: Replace process's address space
     */

    /* Destroy old address space if it's not the kernel's */
    if (old_cr3 != as_get_kernel()->cr3) {
        address_space_t old_as;
        old_as.cr3 = old_cr3;
        old_as.ref_count = 1;
        old_as.user_pages = 0;
        as_destroy(&old_as);
    }

    /* Update process with new address space */
    proc->cr3 = new_as->cr3;
    proc->entry_point = elf_result.entry_point;
    proc->user_stack = sp;

    /* Update process name to executable name */
    const char *basename = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            basename = p + 1;
        }
    }
    int i;
    for (i = 0; i < 31 && basename[i]; i++) {
        proc->name[i] = basename[i];
    }
    proc->name[i] = '\0';

    /* Close file descriptors marked FD_CLOEXEC */
    if (proc->fd_table) {
        fd_table_close_cloexec(proc->fd_table);
    }

    INFO("sys_execve: loaded '%s', entry=0x%llx, sp=0x%llx",
         proc->name, elf_result.entry_point, sp);

    /*
     * Step 5: Switch to new address space and return to user mode
     * We modify the syscall frame to return to the new entry point
     */
    as_switch(new_as);

    /* Modify frame to return to new program */
    frame->rcx = elf_result.entry_point;  /* New RIP */
    frame->rsp = sp;                       /* New RSP */
    frame->rax = 0;                        /* Return value (not really used) */
    frame->r11 = 0x202;                    /* RFLAGS: IF=1 */

    /* Clear other registers for security */
    frame->rdi = 0;
    frame->rsi = 0;
    frame->rdx = 0;
    frame->r8 = 0;
    frame->r9 = 0;
    frame->r10 = 0;
    frame->rbx = 0;
    frame->rbp = 0;
    frame->r12 = 0;
    frame->r13 = 0;
    frame->r14 = 0;
    frame->r15 = 0;

    /* The kfree for new_as struct is intentionally skipped -
     * we've taken ownership of the cr3, the struct can leak for now.
     * In a production kernel, we'd track address spaces properly. */

    /*
     * Return normally - the modified frame will cause SYSRET to
     * jump to the new entry point with the new stack
     */
    return 0;
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

    /*
     * Handle syscalls that need the full frame specially
     */
    if (syscall_num == SYS_FORK) {
        return sys_fork_impl(frame);
    }
    if (syscall_num == SYS_EXECVE) {
        return sys_execve_impl(frame);
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
    syscall_register(SYS_GETPPID, sys_getppid);
    /* Note: SYS_FORK is handled specially in syscall_dispatch */

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
