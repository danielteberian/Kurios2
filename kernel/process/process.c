/* process.c - Process Management */

#include "process.h"
#include "../include/types.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../mm/slab.h"
#include "../sync/spinlock.h"
#include "../debug/debug.h"
#include "../drivers/pit.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/string.h"
#include "../fs/fd_table.h"
#include "../signal/signal.h"

/* Kernel stack size: 16KB (4 pages) - same as thread stacks */
#define KERNEL_STACK_SIZE   (16 * 1024)
#define KERNEL_STACK_ORDER  2  /* 2^2 = 4 pages */

/* Process table - maps PID to process structure */
static process_t *process_table[MAX_PROCESSES];

/* Next PID to allocate */
static pid_t next_pid = 1;

/* Lock for process table and PID allocation */
static spinlock_t process_lock = SPINLOCK_INIT;

/* Current process (per-CPU, but we're single CPU for now) */
static process_t * volatile current_process = NULL;

/* Total active processes */
static uint32_t active_processes = 0;

/* Subsystem initialized flag */
static volatile bool initialized = false;

/*
 * State name strings
 */
static const char *state_names[] = {
    [PROC_UNUSED]   = "UNUSED",
    [PROC_EMBRYO]   = "EMBRYO",
    [PROC_READY]    = "READY",
    [PROC_RUNNING]  = "RUNNING",
    [PROC_BLOCKED]  = "BLOCKED",
    [PROC_ZOMBIE]   = "ZOMBIE",
    [PROC_DEAD]     = "DEAD"
};

/*
 * Copy process name safely
 */
static void copy_process_name(process_t *proc, const char *name) {
    if (!name) {
        proc->name[0] = '\0';
        return;
    }
    int i;
    for (i = 0; i < 31 && name[i]; i++) {
        proc->name[i] = name[i];
    }
    proc->name[i] = '\0';
}

/*
 * Allocate a PID (must hold process_lock)
 * Returns PID_INVALID if no PID available
 */
static pid_t alloc_pid_locked(void) {
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        pid_t pid = (next_pid + i) % MAX_PROCESSES;
        if (pid == 0) {
            pid = 1;  /* Skip PID 0 - reserved for kernel/init */
        }
        if (process_table[pid] == NULL) {
            next_pid = (pid + 1) % MAX_PROCESSES;
            if (next_pid == 0) next_pid = 1;
            return pid;
        }
    }
    return PID_INVALID;
}

/*
 * Initialize process management subsystem
 */
void process_init(void) {
    INFO("Initializing process management...");

    spin_init(&process_lock);

    /* Clear process table */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_table[i] = NULL;
    }

    /* Create process 0 (kernel process) to represent the boot context
     * This process owns the kernel address space and boot thread */
    process_t *kernel_proc = kmalloc(sizeof(process_t));
    if (!kernel_proc) {
        panic("Failed to allocate kernel process");
    }

    memset(kernel_proc, 0, sizeof(process_t));
    kernel_proc->pid = 0;
    kernel_proc->state = PROC_RUNNING;
    kernel_proc->cr3 = vmm_get_cr3();  /* Use current page tables */
    kernel_proc->parent_pid = 0;       /* Kernel is its own parent */
    kernel_proc->exit_code = 0;
    kernel_proc->kernel_stack = NULL;  /* Boot stack managed by bootloader */
    kernel_proc->kernel_stack_size = 0;
    kernel_proc->kernel_rsp = 0;
    kernel_proc->main_thread = thread_current();
    kernel_proc->start_time = pit_get_ticks();
    kernel_proc->cpu_time = 0;
    kernel_proc->fd_table = fd_table_create();
    if (!kernel_proc->fd_table) {
        panic("Failed to create fd table for kernel process");
    }
    kernel_proc->signals = kmalloc(sizeof(signal_state_t));
    if (!kernel_proc->signals) {
        panic("Failed to create signal state for kernel process");
    }
    signal_state_init(kernel_proc->signals);
    kernel_proc->entry_point = 0;
    kernel_proc->user_stack = 0;
    copy_process_name(kernel_proc, "kernel");

    process_table[0] = kernel_proc;
    current_process = kernel_proc;
    active_processes = 1;

    initialized = true;
    INFO("Process management initialized: kernel process PID 0");
}

/*
 * Create a new kernel process
 */
process_t *process_create(const char *name) {
    uint64_t flags = spin_lock_irqsave(&process_lock);

    /* Allocate process structure */
    process_t *proc = kmalloc(sizeof(process_t));
    if (!proc) {
        spin_unlock_irqrestore(&process_lock, flags);
        ERROR("Failed to allocate process structure");
        return NULL;
    }

    /* Allocate PID */
    pid_t pid = alloc_pid_locked();
    if (pid == PID_INVALID) {
        kfree(proc);
        spin_unlock_irqrestore(&process_lock, flags);
        ERROR("No free PIDs (max %d processes)", MAX_PROCESSES);
        return NULL;
    }

    /* Allocate kernel stack */
    uint64_t stack_phys = alloc_pages(KERNEL_STACK_ORDER);
    if (stack_phys == 0) {
        kfree(proc);
        spin_unlock_irqrestore(&process_lock, flags);
        ERROR("Failed to allocate kernel stack for process");
        return NULL;
    }

    /* Initialize process structure */
    memset(proc, 0, sizeof(process_t));
    proc->pid = pid;
    proc->state = PROC_EMBRYO;
    proc->cr3 = vmm_get_cr3();  /* For now, share kernel address space */
    proc->parent_pid = current_process ? current_process->pid : 0;
    proc->exit_code = 0;
    proc->kernel_stack = (void *)stack_phys;
    proc->kernel_stack_size = KERNEL_STACK_SIZE;
    /* Kernel RSP starts at top of stack (stack grows down) */
    proc->kernel_rsp = stack_phys + KERNEL_STACK_SIZE;
    proc->main_thread = NULL;  /* Will be set when thread is created */
    proc->start_time = pit_get_ticks();
    proc->cpu_time = 0;
    proc->fd_table = fd_table_create();
    if (!proc->fd_table) {
        free_pages(stack_phys, KERNEL_STACK_ORDER);
        kfree(proc);
        spin_unlock_irqrestore(&process_lock, flags);
        ERROR("Failed to create fd table for process");
        return NULL;
    }
    proc->signals = kmalloc(sizeof(signal_state_t));
    if (!proc->signals) {
        fd_table_destroy(proc->fd_table);
        free_pages(stack_phys, KERNEL_STACK_ORDER);
        kfree(proc);
        spin_unlock_irqrestore(&process_lock, flags);
        ERROR("Failed to create signal state for process");
        return NULL;
    }
    signal_state_init(proc->signals);
    proc->entry_point = 0;
    proc->user_stack = 0;
    proc->brk = 0x10000000;  /* Default heap start */
    proc->pgrp = pid;        /* Process group = own PID by default */
    proc->session_id = current_process ? current_process->session_id : pid;
    strncpy(proc->cwd, "/", sizeof(proc->cwd));  /* Default to root */
    copy_process_name(proc, name);

    /* Add to process table */
    process_table[pid] = proc;
    active_processes++;

    spin_unlock_irqrestore(&process_lock, flags);

    DEBUG("Created process '%s' PID %u, kernel stack 0x%llx-0x%llx",
          name, pid, stack_phys, stack_phys + KERNEL_STACK_SIZE);

    return proc;
}

/*
 * Destroy a process and free all resources
 */
void process_destroy(process_t *proc) {
    if (!proc) {
        return;
    }

    /* Can only destroy ZOMBIE or DEAD processes */
    if (proc->state != PROC_ZOMBIE && proc->state != PROC_DEAD) {
        WARN("Cannot destroy process %u in state %s",
             proc->pid, process_state_name(proc->state));
        return;
    }

    uint64_t flags = spin_lock_irqsave(&process_lock);

    pid_t pid = proc->pid;

    /* Free kernel stack (if allocated) */
    if (proc->kernel_stack) {
        free_pages((uint64_t)proc->kernel_stack, KERNEL_STACK_ORDER);
    }

    /* Free file descriptor table */
    if (proc->fd_table) {
        fd_table_destroy(proc->fd_table);
        proc->fd_table = NULL;
    }

    /* Free signal state */
    if (proc->signals) {
        kfree(proc->signals);
        proc->signals = NULL;
    }

    /* TODO: Free user address space when implemented */

    /* Remove from process table */
    process_table[pid] = NULL;
    active_processes--;

    /* Free process structure */
    kfree(proc);

    spin_unlock_irqrestore(&process_lock, flags);

    DEBUG("Destroyed process PID %u", pid);
}

/*
 * Get process by PID
 */
process_t *process_get_by_pid(pid_t pid) {
    if (pid >= MAX_PROCESSES) {
        return NULL;
    }
    /* No lock needed - read of pointer is atomic */
    return process_table[pid];
}

/*
 * Get current process
 */
process_t *process_current(void) {
    return current_process;
}

/*
 * Set current process
 */
void process_set_current(process_t *proc) {
    current_process = proc;
}

/*
 * Mark a process as exited
 */
void process_exit(process_t *proc, int exit_code) {
    if (!proc) {
        return;
    }

    uint64_t flags = spin_lock_irqsave(&process_lock);

    proc->state = PROC_ZOMBIE;
    proc->exit_code = exit_code;

    spin_unlock_irqrestore(&process_lock, flags);

    DEBUG("Process '%s' PID %u exited with code %d",
          proc->name, proc->pid, exit_code);
}

/*
 * Find a zombie child process
 */
process_t *process_find_zombie_child(pid_t parent_pid, pid_t child_pid) {
    uint64_t flags = spin_lock_irqsave(&process_lock);

    for (uint32_t i = 1; i < MAX_PROCESSES; i++) {  /* Skip PID 0 (kernel) */
        process_t *proc = process_table[i];
        if (!proc) {
            continue;
        }

        /* Check if this is a child of the specified parent */
        if (proc->parent_pid != parent_pid) {
            continue;
        }

        /* If child_pid is specified, only match that specific child */
        if (child_pid != (pid_t)-1 && proc->pid != child_pid) {
            continue;
        }

        /* Check if child is a zombie */
        if (proc->state == PROC_ZOMBIE) {
            spin_unlock_irqrestore(&process_lock, flags);
            return proc;
        }
    }

    spin_unlock_irqrestore(&process_lock, flags);
    return NULL;
}

/*
 * Check if a process has any children
 */
bool process_has_children(pid_t parent_pid) {
    uint64_t flags = spin_lock_irqsave(&process_lock);

    for (uint32_t i = 1; i < MAX_PROCESSES; i++) {  /* Skip PID 0 */
        process_t *proc = process_table[i];
        if (proc && proc->parent_pid == parent_pid &&
            proc->state != PROC_DEAD && proc->state != PROC_UNUSED) {
            spin_unlock_irqrestore(&process_lock, flags);
            return true;
        }
    }

    spin_unlock_irqrestore(&process_lock, flags);
    return false;
}

/*
 * Get process state name
 */
const char *process_state_name(process_state_t state) {
    if (state < ARRAY_SIZE(state_names)) {
        return state_names[state];
    }
    return "UNKNOWN";
}

/*
 * Dump all processes
 */
void process_dump_all(void) {
    kprintf("\n=== Process Table (%u active) ===\n", active_processes);
    kprintf("  PID  STATE     PPID  NAME                 CR3              KSTACK\n");
    kprintf("  ---  --------  ----  -------------------  ---------------  ---------------\n");

    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *proc = process_table[i];
        if (proc) {
            kprintf("  %3u  %-8s  %4u  %-19s  0x%013llx  0x%013llx\n",
                    proc->pid,
                    process_state_name(proc->state),
                    proc->parent_pid,
                    proc->name,
                    proc->cr3,
                    (uint64_t)proc->kernel_stack);
        }
    }
    kprintf("\n");
}

/*
 * Check if initialized
 */
bool process_is_initialized(void) {
    return initialized;
}

/*
 * Get process count
 */
uint32_t process_count(void) {
    return active_processes;
}

#ifdef DEBUG_TESTS
/*
 * Process subsystem tests
 */
void process_run_tests(void) {
    kprintf("\n=== Process Subsystem Tests ===\n");

    /* Test 1: Check kernel process exists */
    process_t *kernel = process_get_by_pid(0);
    kprintf("  Test 1 - Kernel process exists: %s\n",
            (kernel && kernel->pid == 0) ? "OK" : "FAIL");

    /* Test 2: Check current process is kernel */
    process_t *current = process_current();
    kprintf("  Test 2 - Current process is kernel: %s\n",
            (current == kernel) ? "OK" : "FAIL");

    /* Test 3: Create a test process */
    process_t *test1 = process_create("test_proc1");
    kprintf("  Test 3 - Create test process: %s (PID %u)\n",
            (test1 && test1->pid > 0) ? "OK" : "FAIL",
            test1 ? test1->pid : 0);

    /* Test 4: Verify test process properties */
    bool props_ok = test1 &&
                    test1->state == PROC_EMBRYO &&
                    test1->parent_pid == 0 &&
                    test1->kernel_stack != NULL;
    kprintf("  Test 4 - Process properties: %s\n",
            props_ok ? "OK" : "FAIL");

    /* Test 5: Create second process */
    process_t *test2 = process_create("test_proc2");
    kprintf("  Test 5 - Create second process: %s (PID %u)\n",
            (test2 && test2->pid != test1->pid) ? "OK" : "FAIL",
            test2 ? test2->pid : 0);

    /* Test 6: Process lookup by PID */
    process_t *found = process_get_by_pid(test1->pid);
    kprintf("  Test 6 - Lookup by PID: %s\n",
            (found == test1) ? "OK" : "FAIL");

    /* Test 7: Process count */
    uint32_t count = process_count();
    kprintf("  Test 7 - Process count: %u %s\n",
            count, (count == 3) ? "OK" : "FAIL");  /* kernel + test1 + test2 */

    /* Test 8: Exit process */
    process_exit(test1, 42);
    kprintf("  Test 8 - Exit process: %s (code=%d)\n",
            (test1->state == PROC_ZOMBIE && test1->exit_code == 42) ? "OK" : "FAIL",
            test1->exit_code);

    /* Test 9: Destroy process */
    process_destroy(test1);
    found = process_get_by_pid(test1->pid);  /* Use old PID */
    kprintf("  Test 9 - Destroy process: %s\n",
            (found == NULL) ? "OK" : "FAIL");

    /* Test 10: Verify count decreased */
    count = process_count();
    kprintf("  Test 10 - Count after destroy: %u %s\n",
            count, (count == 2) ? "OK" : "FAIL");  /* kernel + test2 */

    /* Clean up test2 */
    process_exit(test2, 0);
    process_destroy(test2);

    /* Dump final state */
    process_dump_all();

    kprintf("  Process tests complete.\n\n");
}
#endif /* DEBUG_TESTS */
