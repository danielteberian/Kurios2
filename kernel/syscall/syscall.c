/* syscall.c - System Call Implementation */

#include "syscall.h"
#include "../debug/debug.h"
#include "../arch/x86_64/gdt.h"
#include "../include/types.h"
#include "../mm/as.h"
#include "../mm/uaccess.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../mm/page_cache.h"
#include "../process/process.h"
#include "../sched/sched.h"
#include "../sched/thread.h"
#include "../loader/elf_loader.h"
#include "../fs/vfs.h"
#include "../fs/fd_table.h"
#include "../fs/pipe.h"
#include "../mm/slab.h"
#include "../drivers/keyboard.h"
#include "../drivers/pit.h"
#include "../drivers/rtc.h"
#include "../drivers/hpet.h"
#include "../drivers/tty.h"
#include "../signal/signal.h"
#include "../arch/x86_64/cpu.h"
#include "lib/string.h"

/*
 * Model Specific Registers for SYSCALL/SYSRET
 */
#define MSR_EFER        0xC0000080  /* Extended Feature Enable Register */
#define MSR_STAR        0xC0000081  /* Segment selectors for SYSCALL/SYSRET */
#define MSR_LSTAR       0xC0000082  /* SYSCALL entry point (64-bit) */
#define MSR_CSTAR       0xC0000083  /* SYSCALL entry point (compat mode, unused) */
#define MSR_SFMASK      0xC0000084  /* RFLAGS mask for SYSCALL */

/* EFER bits (defined in cpu.h, guard against redefinition) */
#ifndef EFER_SCE
#define EFER_SCE        (1UL << 0)  /* SYSCALL/SYSRET enable */
#endif

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
 * sys_read - read from a file descriptor
 *
 * For stdin (fd 0): reads from keyboard
 * For other fds: reads from VFS
 */
#define READ_CHUNK_SIZE 256

static int64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_read(fd=%llu, buf=0x%llx, count=%llu)", fd, buf, count);

    /* Zero count is valid - just return 0 */
    if (count == 0) {
        return 0;
    }

    /* Validate user buffer */
    if (!access_ok((void *)buf, count)) {
        return -EFAULT;
    }

    /* Handle stdin - read from TTY (with line discipline) */
    if (fd == 0) {
        char kbuf[READ_CHUNK_SIZE];
        uint64_t total = 0;

        while (total < count) {
            uint64_t chunk = count - total;
            if (chunk > READ_CHUNK_SIZE) {
                chunk = READ_CHUNK_SIZE;
            }

            /* Read from TTY - handles canonical mode, echo, signals */
            ssize_t n = tty_read(kbuf, chunk);
            if (n < 0) {
                if (n == -11) {  /* EAGAIN - no data ready yet */
                    /* In canonical mode, block until line ready.
                     * Enable interrupts so keyboard IRQ can fire. */
                    sti();
                    hlt();  /* Wait for interrupt (keyboard input) */
                    continue;
                }
                return total > 0 ? (int64_t)total : (int64_t)n;
            }
            if (n == 0) {
                break;  /* EOF */
            }

            /* Copy to user space */
            int err = copy_to_user((void *)(buf + total), kbuf, n);
            if (err < 0) {
                return total > 0 ? (int64_t)total : err;
            }

            total += n;

            /* In canonical mode, return after one read (line at a time) */
            break;
        }

        return (int64_t)total;
    }

    /* Handle VFS files (fd > 2) */
    if (fd > 2) {
        char kbuf[READ_CHUNK_SIZE];
        uint64_t total = 0;

        while (total < count) {
            uint64_t chunk = count - total;
            if (chunk > READ_CHUNK_SIZE) {
                chunk = READ_CHUNK_SIZE;
            }

            ssize_t n = vfs_read((int)fd, kbuf, chunk);
            if (n < 0) {
                return total > 0 ? (int64_t)total : (int64_t)n;
            }
            if (n == 0) {
                break;  /* EOF */
            }

            if (copy_to_user((char *)buf + total, kbuf, n) < 0) {
                return total > 0 ? (int64_t)total : -EFAULT;
            }

            total += n;
            if ((uint64_t)n < chunk) {
                break;  /* Short read */
            }
        }

        return (int64_t)total;
    }

    /* fd 1, 2 (stdout/stderr) are not readable */
    return -EBADF;
}

/*
 * sys_write - write to a file descriptor
 *
 * Uses the process fd table to write through VFS, which allows
 * stdout/stderr to properly go through TTY to both VGA and serial.
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

    /* fd 0 (stdin) is not writable */
    if (fd == 0) {
        return -EBADF;
    }

    /* Write through VFS - handles stdout/stderr via /dev/console TTY */
    char kbuf[WRITE_CHUNK_SIZE];
    uint64_t total = 0;

    while (total < count) {
        uint64_t chunk = count - total;
        if (chunk > WRITE_CHUNK_SIZE) {
            chunk = WRITE_CHUNK_SIZE;
        }

        if (copy_from_user(kbuf, (const char *)buf + total, chunk) < 0) {
            return total > 0 ? (int64_t)total : -EFAULT;
        }

        ssize_t n = vfs_write((int)fd, kbuf, chunk);
        if (n < 0) {
            return total > 0 ? (int64_t)total : (int64_t)n;
        }

        total += n;
        if ((uint64_t)n < chunk) {
            break;  /* Short write */
        }
    }

    return (int64_t)total;
}

/*
 * sys_open - open a file
 *
 * @param pathname  Path to file (user pointer)
 * @param flags     Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.)
 * @param mode      File mode for O_CREAT (ignored for now)
 * @return File descriptor on success, negative error on failure
 */
#define OPEN_PATH_MAX 256

static int64_t sys_open(uint64_t pathname, uint64_t flags, uint64_t mode,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)mode; (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_open(pathname=0x%llx, flags=0x%llx, mode=0x%llx)", pathname, flags, mode);

    /* Copy path from user space */
    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;  /* -EFAULT or -ENAMETOOLONG */
    }

    /* Call VFS open */
    int fd = vfs_open(path, (uint32_t)flags);
    if (fd < 0) {
        DEBUG("sys_open: vfs_open failed: %d", fd);
        /* Convert VFS error codes to syscall error codes */
        return (int64_t)fd;
    }

    DEBUG("sys_open: opened '%s' as fd %d", path, fd);
    return (int64_t)fd;
}

/*
 * sys_close - close a file descriptor
 *
 * @param fd  File descriptor to close
 * @return 0 on success, -EBADF if fd is invalid
 */
static int64_t sys_close(uint64_t fd, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_close(fd=%llu)", fd);

    /* Don't allow closing stdin/stdout/stderr for now */
    if (fd <= 2) {
        DEBUG("sys_close: cannot close fd %llu (reserved)", fd);
        return -EBADF;
    }

    /* Validate fd range */
    if (fd >= VFS_MAX_FDS) {
        return -EBADF;
    }

    /* Close via VFS - it handles the fd lookup and cleanup */
    vfs_close((int)fd);

    return 0;
}

/*
 * sys_lseek - reposition file offset
 *
 * @param fd      File descriptor
 * @param offset  Offset to seek to
 * @param whence  SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New offset on success, negative error on failure
 */
static int64_t sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_lseek(fd=%llu, offset=%lld, whence=%llu)", fd, (int64_t)offset, whence);

    /* stdin/stdout/stderr are not seekable */
    if (fd < 3) {
        return -ESPIPE;
    }

    return vfs_seek((int)fd, (int64_t)offset, (int)whence);
}

/*
 * sys_fstat - get file status
 *
 * @param fd      File descriptor
 * @param statbuf User pointer to vfs_stat_t structure
 * @return 0 on success, negative error on failure
 */
static int64_t sys_fstat(uint64_t fd, uint64_t statbuf, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_fstat(fd=%llu, statbuf=0x%llx)", fd, statbuf);

    if (!access_ok((void *)statbuf, sizeof(vfs_stat_t))) {
        return -EFAULT;
    }

    /* For stdin/stdout/stderr, return a minimal stat */
    if (fd < 3) {
        vfs_stat_t kstat;
        memset(&kstat, 0, sizeof(kstat));
        kstat.type = VFS_CHARDEV;
        kstat.permissions = VFS_PERM_READ | VFS_PERM_WRITE;
        kstat.nlink = 1;

        if (copy_to_user((void *)statbuf, &kstat, sizeof(kstat)) < 0) {
            return -EFAULT;
        }
        return 0;
    }

    vfs_stat_t kstat;
    int ret = vfs_fstat((int)fd, &kstat);
    if (ret < 0) {
        return ret;
    }

    if (copy_to_user((void *)statbuf, &kstat, sizeof(kstat)) < 0) {
        return -EFAULT;
    }

    return 0;
}

/*
 * sys_dup - duplicate a file descriptor
 *
 * @param oldfd  File descriptor to duplicate
 * @return New file descriptor on success, negative error on failure
 */
static int64_t sys_dup(uint64_t oldfd, uint64_t arg2, uint64_t arg3,
                       uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_dup(oldfd=%llu)", oldfd);

    /* stdin/stdout/stderr duplication not supported for now */
    if (oldfd < 3) {
        return -EBADF;
    }

    return vfs_dup((int)oldfd);
}

/*
 * sys_dup2 - duplicate a file descriptor to a specific fd
 *
 * @param oldfd  File descriptor to duplicate
 * @param newfd  Target file descriptor
 * @return newfd on success, negative error on failure
 */
static int64_t sys_dup2(uint64_t oldfd, uint64_t newfd, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_dup2(oldfd=%llu, newfd=%llu)", oldfd, newfd);

    /* stdin/stdout/stderr cannot be targets for now */
    if (newfd < 3) {
        return -EBADF;
    }

    /* stdin/stdout/stderr duplication not supported for now */
    if (oldfd < 3) {
        return -EBADF;
    }

    return vfs_dup2((int)oldfd, (int)newfd);
}

/*
 * sys_fcntl - file control operations
 *
 * @param fd   File descriptor
 * @param cmd  Command (F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL)
 * @param arg  Command-specific argument
 */
static int64_t sys_fcntl(uint64_t fd, uint64_t cmd, uint64_t arg,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    DEBUG("sys_fcntl(fd=%llu, cmd=%llu, arg=%llu)", fd, cmd, arg);

    process_t *proc = process_current();
    if (!proc || !proc->fd_table) {
        return -EBADF;
    }

    file_t *file = fd_table_get(proc->fd_table, (int)fd);
    if (!file) {
        return -EBADF;
    }

    switch (cmd) {
    case F_DUPFD: {
        /* Duplicate fd to lowest available >= arg */
        int min_fd = (int)arg;
        if (min_fd < 0 || min_fd >= FD_MAX) {
            return -EINVAL;
        }
        /* Find lowest available fd >= min_fd */
        for (int new_fd = min_fd; new_fd < FD_MAX; new_fd++) {
            if (!fd_table_get(proc->fd_table, new_fd)) {
                /* Found free slot, duplicate file reference */
                file->ref_count++;
                int result = fd_table_alloc_at(proc->fd_table, new_fd, file, 0);
                if (result < 0) {
                    file->ref_count--;
                    return -EMFILE;
                }
                return new_fd;
            }
        }
        return -EMFILE;
    }

    case F_GETFD:
        /* Get fd flags (FD_CLOEXEC) */
        return fd_table_get_flags(proc->fd_table, (int)fd);

    case F_SETFD:
        /* Set fd flags (only FD_CLOEXEC is valid) */
        if (fd_table_set_flags(proc->fd_table, (int)fd, (uint32_t)(arg & FD_CLOEXEC)) < 0) {
            return -EBADF;
        }
        return 0;

    case F_GETFL:
        /* Get file status flags */
        return file->flags;

    case F_SETFL:
        /* Set file status flags (only O_APPEND and O_NONBLOCK can be changed) */
        file->flags = (file->flags & ~(O_APPEND | O_NONBLOCK)) |
                      (arg & (O_APPEND | O_NONBLOCK));
        return 0;

    default:
        return -EINVAL;
    }
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
 * sys_wait4 - wait for a child process
 *
 * @param pid      Child PID to wait for, or -1 for any child
 * @param status   Pointer to store status (user pointer, can be NULL)
 * @param options  Options (WNOHANG, WUNTRACED)
 * @param rusage   Resource usage (ignored for now)
 * @return Child PID on success, 0 if WNOHANG and no child exited,
 *         or negative error code
 *
 * This implements waitpid() semantics via the wait4 syscall.
 */
static int64_t sys_wait4(uint64_t pid_arg, uint64_t status_ptr,
                         uint64_t options, uint64_t rusage,
                         uint64_t arg5, uint64_t arg6) {
    (void)rusage; (void)arg5; (void)arg6;

    pid_t target_pid = (pid_t)pid_arg;
    int *status = (int *)status_ptr;

    DEBUG("sys_wait4(pid=%d, status=0x%llx, options=0x%llx)",
          (int32_t)target_pid, status_ptr, options);

    /* Validate status pointer if provided */
    if (status && !access_ok(status, sizeof(int))) {
        return -EFAULT;
    }

    process_t *parent = process_current();
    if (!parent) {
        return -ESRCH;
    }

    /*
     * Wait for child process(es)
     *
     * pid_arg meanings:
     *   < -1: Wait for any child in process group |pid|  (not implemented)
     *   == -1: Wait for any child
     *   == 0:  Wait for any child in same process group (not implemented)
     *   > 0:   Wait for specific child with that PID
     */
    pid_t wait_for = (target_pid == 0 || (int32_t)target_pid < -1) ?
                     (pid_t)-1 : target_pid;

    while (1) {
        /* Check if we have any children at all */
        if (!process_has_children(parent->pid)) {
            return -ECHILD;  /* No children to wait for */
        }

        /* Priority 1: Check for zombie children */
        process_t *child = process_find_zombie_child(parent->pid, wait_for);
        if (child) {
            /* Found a zombie child - reap it */
            pid_t child_pid = child->pid;
            int exit_code = child->exit_code;

            DEBUG("sys_wait4: found zombie child PID %u, exit_code=%d",
                  child_pid, exit_code);

            /* Encode status based on exit reason */
            int kstatus;
            if (exit_code >= 128) {
                /* Died by signal: signal number in low 7 bits */
                kstatus = exit_code - 128;
            } else {
                /* Normal exit: exit code in bits 8-15 */
                kstatus = (exit_code & 0xff) << 8;
            }

            if (status) {
                if (copy_to_user(status, &kstatus, sizeof(int)) < 0) {
                    return -EFAULT;
                }
            }

            /* Destroy the child process (reap it) */
            process_destroy(child);

            return (int64_t)child_pid;
        }

        /* Priority 2: Check for stopped children (if WUNTRACED) */
        extern process_t *process_find_stopped_child(uint32_t parent_pid, uint32_t child_pid);
        if (options & WUNTRACED) {
            child = process_find_stopped_child(parent->pid, wait_for);
            if (child) {
                pid_t child_pid = child->pid;
                int stop_sig = child->stop_signal;

                DEBUG("sys_wait4: found stopped child PID %u, signal=%d",
                      child_pid, stop_sig);

                /* Mark as reported */
                child->stop_reported = true;

                /* Encode status: 0x7f | (signal << 8) */
                int kstatus = 0x7f | ((stop_sig & 0xff) << 8);

                if (status) {
                    if (copy_to_user(status, &kstatus, sizeof(int)) < 0) {
                        return -EFAULT;
                    }
                }

                return (int64_t)child_pid;
            }
        }

        /* Priority 3: Check for continued children (if WCONTINUED) */
        extern process_t *process_find_continued_child(uint32_t parent_pid, uint32_t child_pid);
        if (options & WCONTINUED) {
            child = process_find_continued_child(parent->pid, wait_for);
            if (child) {
                pid_t child_pid = child->pid;

                DEBUG("sys_wait4: found continued child PID %u", child_pid);

                /* Mark as reported */
                child->continue_reported = true;

                /* Encode status: 0xffff (special marker for WIFCONTINUED) */
                int kstatus = 0xffff;

                if (status) {
                    if (copy_to_user(status, &kstatus, sizeof(int)) < 0) {
                        return -EFAULT;
                    }
                }

                return (int64_t)child_pid;
            }
        }

        /* No child state change found */
        if (options & WNOHANG) {
            /* Don't block - return 0 indicating no child changed state */
            return 0;
        }

        /* Block waiting for a child
         * In a real implementation, we'd sleep and be woken when a child exits.
         * For now, just yield and try again (busy-wait). */
        sched_reschedule();
    }
}

/*
 * sys_kill - send a signal to a process
 *
 * @param pid     Process ID to signal (or special values)
 * @param sig     Signal number to send
 * @return 0 on success, negative error on failure
 *
 * Special pid values:
 *   > 0:  Send to specific process
 *   == 0: Send to all processes in current process group
 *   == -1: Send to all processes except init (not implemented)
 *   < -1: Send to process group -pid
 */
static int64_t sys_kill(uint64_t pid_arg, uint64_t sig,
                        uint64_t arg3, uint64_t arg4,
                        uint64_t arg5, uint64_t arg6)
{
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    int32_t pid = (int32_t)pid_arg;
    int signum = (int)sig;

    DEBUG("sys_kill(pid=%d, sig=%d)", pid, signum);

    /* Validate signal number */
    if (signum < 0 || signum >= NSIG) {
        return -EINVAL;
    }

    /* pid > 0: Send to specific process */
    if (pid > 0) {
        return signal_send((uint32_t)pid, signum);
    }

    /* Need pgrp.h for process group operations */
    extern int pgrp_send_signal(uint32_t pgrp, int sig);

    /* pid == 0: Send to current process group */
    if (pid == 0) {
        process_t *proc = process_current();
        if (!proc) {
            return -ESRCH;
        }
        int ret = pgrp_send_signal(proc->pgrp, signum);
        return ret > 0 ? 0 : ret;  /* Return 0 on success, error otherwise */
    }

    /* pid == -1: Send to all processes except init */
    if (pid == -1) {
        WARN("sys_kill: kill(-1) not implemented yet");
        return -ENOSYS;
    }

    /* pid < -1: Send to process group -pid */
    if (pid < -1) {
        uint32_t pgrp = (uint32_t)(-pid);
        int ret = pgrp_send_signal(pgrp, signum);
        return ret > 0 ? 0 : ret;  /* Return 0 on success, error otherwise */
    }

    return -EINVAL;
}

/* ============================================================================
 * NEW SYSCALLS - File/Directory Operations
 * ============================================================================ */

/*
 * sys_stat - get file status by pathname
 */
static int64_t sys_stat(uint64_t pathname, uint64_t statbuf, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    if (!access_ok((void *)statbuf, sizeof(vfs_stat_t))) {
        return -EFAULT;
    }

    vfs_stat_t kstat;
    int ret = vfs_stat(path, &kstat);
    if (ret < 0) {
        return ret;
    }

    if (copy_to_user((void *)statbuf, &kstat, sizeof(kstat)) < 0) {
        return -EFAULT;
    }

    return 0;
}

/*
 * sys_lstat - get file status (don't follow symlinks)
 * For symlinks, returns info about the link itself, not the target
 */
static int64_t sys_lstat(uint64_t pathname, uint64_t statbuf, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    if (!access_ok((void *)statbuf, sizeof(vfs_stat_t))) {
        return -EFAULT;
    }

    /* Note: vfs_stat doesn't follow symlinks currently, so this is the same as stat */
    /* When symlink following is added to stat, this should not follow */
    vfs_stat_t kstat;
    int ret = vfs_stat(path, &kstat);
    if (ret < 0) {
        return ret;
    }

    if (copy_to_user((void *)statbuf, &kstat, sizeof(kstat)) < 0) {
        return -EFAULT;
    }

    return 0;
}

/*
 * sys_access - check file access permissions
 */
static int64_t sys_access(uint64_t pathname, uint64_t mode, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    /* Check if file exists */
    vfs_stat_t st;
    int ret = vfs_stat(path, &st);
    if (ret < 0) {
        return -ENOENT;
    }

    /* For now, just check existence (F_OK) or assume all permissions granted */
    if (mode == F_OK) {
        return 0;
    }

    /* Check permissions - in a real system we'd check against user/group */
    if ((mode & R_OK) && !(st.permissions & VFS_PERM_READ)) {
        return -EACCES;
    }
    if ((mode & W_OK) && !(st.permissions & VFS_PERM_WRITE)) {
        return -EACCES;
    }
    if ((mode & X_OK) && !(st.permissions & VFS_PERM_EXEC)) {
        return -EACCES;
    }

    return 0;
}

/*
 * sys_getcwd - get current working directory
 */
static int64_t sys_getcwd(uint64_t buf, uint64_t size, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (size == 0) {
        return -EINVAL;
    }

    if (!access_ok((void *)buf, size)) {
        return -EFAULT;
    }

    process_t *proc = process_current();
    const char *cwd = proc ? proc->cwd : "/";

    size_t len = strlen(cwd);
    if (len + 1 > size) {
        return -ERANGE;
    }

    if (copy_to_user((void *)buf, cwd, len + 1) < 0) {
        return -EFAULT;
    }

    return (int64_t)buf;
}

/*
 * sys_chdir - change current working directory
 */
static int64_t sys_chdir(uint64_t pathname, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    /* Verify it's a directory */
    vfs_stat_t st;
    int ret = vfs_stat(path, &st);
    if (ret < 0) {
        return -ENOENT;
    }
    if (st.type != VFS_DIR) {
        return -ENOTDIR;
    }

    process_t *proc = process_current();
    if (proc) {
        strncpy(proc->cwd, path, sizeof(proc->cwd) - 1);
        proc->cwd[sizeof(proc->cwd) - 1] = '\0';
    }

    return 0;
}

/*
 * Build path from VFS node by walking parent chain
 */
static int build_path_from_node(vfs_node_t *node, char *buf, size_t size) {
    if (!node || size < 2) return -1;

    /* Handle root */
    if (!node->parent) {
        buf[0] = '/';
        buf[1] = '\0';
        return 0;
    }

    /* Build path in reverse using stack */
    const char *parts[64];
    int depth = 0;
    vfs_node_t *n = node;

    while (n && n->parent && depth < 64) {
        parts[depth++] = n->name;
        n = n->parent;
    }

    /* Construct path */
    char *p = buf;
    char *end = buf + size - 1;

    for (int i = depth - 1; i >= 0 && p < end; i--) {
        *p++ = '/';
        const char *name = parts[i];
        while (*name && p < end) {
            *p++ = *name++;
        }
    }

    if (p == buf) *p++ = '/';  /* Root case */
    *p = '\0';
    return 0;
}

/*
 * sys_fchdir - change working directory by file descriptor
 */
static int64_t sys_fchdir(uint64_t fd, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc || !proc->fd_table) {
        return -EBADF;
    }

    file_t *file = fd_table_get(proc->fd_table, (int)fd);
    if (!file || !file->node) {
        return -EBADF;
    }

    /* Must be a directory */
    if (file->node->type != VFS_DIR) {
        return -ENOTDIR;
    }

    /* Get full path of the directory */
    char path[256];
    if (build_path_from_node(file->node, path, sizeof(path)) < 0) {
        return -ENOENT;
    }

    strncpy(proc->cwd, path, sizeof(proc->cwd) - 1);
    proc->cwd[sizeof(proc->cwd) - 1] = '\0';

    return 0;
}

/*
 * sys_umask - set file mode creation mask
 *
 * @param mask  New umask value
 * @return Previous umask value
 */
static int64_t sys_umask(uint64_t mask, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return 022;  /* Default if no process */
    }

    uint32_t old_mask = proc->umask;
    proc->umask = (uint32_t)(mask & 0777);  /* Only permission bits */
    return old_mask;
}

/*
 * sys_alarm - set an alarm clock for delivery of SIGALRM
 *
 * @param seconds  Number of seconds until SIGALRM (0 cancels any pending alarm)
 * @return Seconds remaining on previous alarm (0 if none)
 */
static int64_t sys_alarm(uint64_t seconds, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return 0;
    }

    uint64_t current_ticks = pit_get_ticks();
    uint64_t old_remaining = 0;

    /* Calculate remaining seconds from previous alarm */
    if (proc->alarm_ticks > current_ticks) {
        old_remaining = (proc->alarm_ticks - current_ticks) / 100;  /* 100 Hz PIT */
    }

    /* Set new alarm or cancel */
    if (seconds == 0) {
        proc->alarm_ticks = 0;  /* Cancel alarm */
    } else {
        proc->alarm_ticks = current_ticks + (seconds * 100);  /* 100 Hz PIT */
    }

    return (int64_t)old_remaining;
}

/*
 * sys_rename - rename a file or directory
 */
static int64_t sys_rename(uint64_t oldpath, uint64_t newpath, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char old_buf[OPEN_PATH_MAX];
    char new_buf[OPEN_PATH_MAX];

    ssize_t len = strncpy_from_user(old_buf, (const char *)oldpath, OPEN_PATH_MAX);
    if (len < 0) {
        return len;
    }

    len = strncpy_from_user(new_buf, (const char *)newpath, OPEN_PATH_MAX);
    if (len < 0) {
        return len;
    }

    return vfs_rename(old_buf, new_buf);
}

/*
 * sys_mkdir - create a directory
 */
static int64_t sys_mkdir(uint64_t pathname, uint64_t mode, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)mode; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    return vfs_mkdir(path);
}

/*
 * sys_mknod - create a special file (device node)
 */
static int64_t sys_mknod(uint64_t pathname, uint64_t mode, uint64_t dev,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    return vfs_mknod(path, (uint32_t)mode, (dev_t)dev);
}

/*
 * sys_rmdir - remove a directory
 */
static int64_t sys_rmdir(uint64_t pathname, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    return vfs_rmdir(path);
}

/*
 * sys_unlink - remove a file
 */
static int64_t sys_unlink(uint64_t pathname, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    return vfs_unlink(path);
}

/*
 * sys_truncate - truncate a file by path
 */
static int64_t sys_truncate(uint64_t pathname, uint64_t length, uint64_t arg3,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    return vfs_truncate(path, length);
}

/*
 * sys_ftruncate - truncate a file by fd
 */
static int64_t sys_ftruncate(uint64_t fd, uint64_t length, uint64_t arg3,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (fd < 3) {
        return -EINVAL;
    }

    /* Get file from fd, then truncate - this is a simplified version */
    /* For a full implementation, we'd need vfs_ftruncate() */
    (void)fd; (void)length;
    return -ENOSYS;  /* Not fully implemented */
}

/*
 * sys_getdents - get directory entries
 */
static int64_t sys_getdents(uint64_t fd, uint64_t dirp, uint64_t count,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    if (!access_ok((void *)dirp, count)) {
        return -EFAULT;
    }

    if (count < sizeof(linux_dirent64_t) + 2) {
        return -EINVAL;
    }

    /* Read directory entries */
    dirent_t dent;
    int ret = vfs_readdir((int)fd, &dent);
    if (ret != VFS_OK) {
        if (ret == -ENOENT) {
            return 0;  /* End of directory */
        }
        return ret;
    }

    /* Calculate entry size (header + name + null + padding) */
    size_t name_len = strlen(dent.name);
    size_t reclen = sizeof(linux_dirent64_t) + name_len + 1;
    reclen = (reclen + 7) & ~7;  /* Align to 8 bytes */

    if (reclen > count) {
        return -EINVAL;
    }

    /* Build the entry in kernel space */
    uint8_t kbuf[256];
    linux_dirent64_t *ent = (linux_dirent64_t *)kbuf;
    ent->d_ino = dent.inode;
    ent->d_off = 0;
    ent->d_reclen = (uint16_t)reclen;
    ent->d_type = (dent.type == VFS_DIR) ? DT_DIR : DT_REG;
    memcpy(ent->d_name, dent.name, name_len + 1);

    if (copy_to_user((void *)dirp, kbuf, reclen) < 0) {
        return -EFAULT;
    }

    return (int64_t)reclen;
}

/*
 * sys_readlink - read symbolic link
 */
static int64_t sys_readlink(uint64_t pathname, uint64_t buf, uint64_t bufsiz,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    if (!pathname || !buf || bufsiz == 0) {
        return -EINVAL;
    }

    /* Copy pathname from user space */
    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    /* Read the symlink */
    ssize_t result = vfs_readlink(path, (char *)buf, bufsiz);
    return result;  /* VFS error codes match syscall error codes */
}

/*
 * sys_symlink - create a symbolic link
 */
static int64_t sys_symlink(uint64_t target, uint64_t linkpath, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (!target || !linkpath) {
        return -EINVAL;
    }

    /* Copy target from user space */
    char target_buf[OPEN_PATH_MAX];
    ssize_t target_len = strncpy_from_user(target_buf, (const char *)target, OPEN_PATH_MAX);
    if (target_len < 0) {
        return target_len;
    }

    /* Copy linkpath from user space */
    char link_buf[OPEN_PATH_MAX];
    ssize_t link_len = strncpy_from_user(link_buf, (const char *)linkpath, OPEN_PATH_MAX);
    if (link_len < 0) {
        return link_len;
    }

    /* Create the symlink */
    int result = vfs_symlink(target_buf, link_buf);
    return result;  /* VFS error codes match syscall error codes */
}

/*
 * sys_link - create a hard link (stub - ramfs doesn't support hard links)
 */
static int64_t sys_link(uint64_t oldpath, uint64_t newpath, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)oldpath; (void)newpath;
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    /* Hard links not supported in ramfs */
    return -ENOSYS;
}

/*
 * sys_chmod - change file permissions
 */
static int64_t sys_chmod(uint64_t pathname, uint64_t mode, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    return vfs_chmod(path, (uint32_t)mode);
}

/*
 * sys_fchmod - change file permissions by fd
 */
static int64_t sys_fchmod(uint64_t fd, uint64_t mode, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    return vfs_fchmod((int)fd, (uint32_t)mode);
}

/*
 * sys_chown - change file owner and group
 */
static int64_t sys_chown(uint64_t pathname, uint64_t owner, uint64_t group,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    char path[OPEN_PATH_MAX];
    ssize_t path_len = strncpy_from_user(path, (const char *)pathname, OPEN_PATH_MAX);
    if (path_len < 0) {
        return path_len;
    }

    return vfs_chown(path, (uint32_t)owner, (uint32_t)group);
}

/*
 * sys_fchown - change file owner and group by fd
 */
static int64_t sys_fchown(uint64_t fd, uint64_t owner, uint64_t group,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    return vfs_fchown((int)fd, (uint32_t)owner, (uint32_t)group);
}

/*
 * sys_lchown - change symlink owner (same as chown for us)
 */
static int64_t sys_lchown(uint64_t pathname, uint64_t owner, uint64_t group,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    /* For symlinks, we don't follow the link - but we treat it same as chown */
    return sys_chown(pathname, owner, group, arg4, arg5, arg6);
}

/*
 * sys_sync - sync all filesystems
 */
static int64_t sys_sync(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    return vfs_sync();
}

/*
 * sys_fsync - sync file data to disk
 */
static int64_t sys_fsync(uint64_t fd, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    return vfs_fsync((int)fd);
}

/*
 * sys_fdatasync - sync file data (same as fsync for us)
 */
static int64_t sys_fdatasync(uint64_t fd, uint64_t arg2, uint64_t arg3,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    /* fdatasync is like fsync but doesn't sync metadata - we treat them the same */
    return sys_fsync(fd, arg2, arg3, arg4, arg5, arg6);
}

/*
 * sys_getrusage - get resource usage (stub)
 * Returns zeros - actual resource tracking not implemented
 */
static int64_t sys_getrusage(uint64_t who, uint64_t usage, uint64_t arg3,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)who; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (!usage) {
        return -EFAULT;
    }

    /* Zero out the rusage structure (simplified: 144 bytes on Linux x86_64) */
    char zero_usage[144] = {0};
    if (copy_to_user((void *)usage, zero_usage, sizeof(zero_usage)) < 0) {
        return -EFAULT;
    }

    return 0;
}

/*
 * sys_times - get process times (stub)
 * Returns zeros - actual time tracking not implemented
 */
static int64_t sys_times(uint64_t buf, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (buf) {
        /* Zero out tms structure (32 bytes: 4 clock_t values) */
        char zero_tms[32] = {0};
        if (copy_to_user((void *)buf, zero_tms, sizeof(zero_tms)) < 0) {
            return -EFAULT;
        }
    }

    /* Return clock ticks since boot (use PIT uptime) */
    extern uint64_t pit_get_uptime_ms(void);
    return (int64_t)(pit_get_uptime_ms() / 10);  /* Assume 100 Hz ticks */
}

/* ============================================================================
 * NEW SYSCALLS - Memory Management
 * ============================================================================ */

/* Process break (heap end) tracking - simple implementation */
static uint64_t process_brk = 0x10000000;  /* Default starting brk */

/*
 * sys_brk - change data segment size (heap management)
 */
static int64_t sys_brk(uint64_t brk, uint64_t arg2, uint64_t arg3,
                       uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return process_brk;
    }

    /* Get current brk */
    uint64_t current_brk = proc->brk ? proc->brk : process_brk;

    /* If brk is 0, just return current brk */
    if (brk == 0) {
        return (int64_t)current_brk;
    }

    /* Don't allow shrinking below initial brk or above limit */
    if (brk < 0x10000000 || brk > 0x80000000) {
        return (int64_t)current_brk;
    }

    /* For now, just update the brk - in a real implementation,
     * we'd allocate/deallocate pages as needed */
    proc->brk = brk;
    return (int64_t)brk;
}

/*
 * sys_mmap - map memory (demand paging)
 *
 * Creates a VMA for the mapping but does NOT allocate physical pages.
 * Pages are allocated on first access by the page fault handler.
 * Supports both anonymous and file-backed mappings.
 */
static int64_t sys_mmap(uint64_t addr, uint64_t length, uint64_t prot,
                        uint64_t flags, uint64_t fd, uint64_t offset) {
    DEBUG("sys_mmap(addr=0x%llx, len=%llu, prot=0x%llx, flags=0x%llx, fd=%llu, off=%llu)",
          addr, length, prot, flags, fd, offset);

    if (length == 0) {
        return -EINVAL;
    }

    process_t *proc = process_current();
    if (!proc) {
        return -ESRCH;
    }

    /* Handle file-backed mappings */
    vfs_node_t *file_node = NULL;
    if (!(flags & MAP_ANONYMOUS) && fd != (uint64_t)-1) {
        file_t *file = fd_table_get(proc->fd_table, (int)fd);
        if (!file || !file->node) {
            return -EBADF;
        }
        /* Only regular files can be mmap'd */
        if (file->node->type != VFS_FILE) {
            return -ENODEV;
        }
        file_node = file->node;
        /* Reference the node for the VMA */
        vfs_node_ref(file_node);
    }

    /* Round length up to page size */
    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Need address space for demand paging */
    if (!proc->as || !proc->as->vmas) {
        /* Fallback to eager allocation if no VMA support */
        DEBUG("sys_mmap: no VMA support, using eager allocation");

        if (file_node) {
            vfs_node_unref(file_node);  /* Can't do file mmap without VMA */
            return -ENOSYS;
        }

        static uint64_t mmap_hint_eager = 0x40000000;
        uint64_t map_addr;
        uint64_t num_pages = length / PAGE_SIZE;

        if (addr && (flags & MAP_FIXED)) {
            map_addr = addr & ~(PAGE_SIZE - 1);
        } else if (addr) {
            map_addr = addr & ~(PAGE_SIZE - 1);
        } else {
            map_addr = mmap_hint_eager;
            mmap_hint_eager += length + PAGE_SIZE;
        }

        uint64_t pte_flags = PTE_USER;
        if (prot & PROT_WRITE) pte_flags |= PTE_WRITABLE;
        if (!(prot & PROT_EXEC)) pte_flags |= PTE_NX;

        address_space_t as;
        as.cr3 = proc->cr3 ? proc->cr3 : as_get_kernel()->cr3;
        as.ref_count = 1;
        as.user_pages = 0;
        as.vmas = NULL;

        int ret = as_alloc_pages(&as, map_addr, num_pages, pte_flags);
        if (ret < 0) {
            return ret;
        }
        return (int64_t)map_addr;
    }

    /* Demand paging path: create VMA without allocating pages */
    uint64_t map_addr;

    if (addr && (flags & MAP_FIXED)) {
        map_addr = addr & ~(PAGE_SIZE - 1);
        /* For MAP_FIXED, check for overlaps and remove existing VMAs */
        if (vma_overlaps(proc->as->vmas, map_addr, map_addr + length)) {
            /* TODO: unmap overlapping regions */
            DEBUG("sys_mmap: MAP_FIXED overlaps existing VMA");
        }
    } else if (addr) {
        /* Try requested address first */
        map_addr = addr & ~(PAGE_SIZE - 1);
        if (vma_overlaps(proc->as->vmas, map_addr, map_addr + length)) {
            /* Find free region instead */
            map_addr = vma_find_free(proc->as->vmas, length, map_addr);
            if (map_addr == 0) {
                return -ENOMEM;
            }
        }
    } else {
        /* Find any free region */
        map_addr = vma_find_free(proc->as->vmas, length, 0);
        if (map_addr == 0) {
            return -ENOMEM;
        }
    }

    /* Build VMA flags from mmap flags */
    uint32_t vma_flags = 0;
    if (file_node) {
        /* File-backed mapping */
    } else {
        vma_flags |= VMA_ANONYMOUS;
    }
    if (prot & PROT_READ)  vma_flags |= VMA_READ;
    if (prot & PROT_WRITE) vma_flags |= VMA_WRITE;
    if (prot & PROT_EXEC)  vma_flags |= VMA_EXEC;
    if (flags & MAP_SHARED) vma_flags |= VMA_SHARED;
    if (flags & MAP_FIXED) vma_flags |= VMA_FIXED;

    /* Build PTE flags for when pages are faulted in */
    uint32_t pte_prot = PTE_USER | PTE_PRESENT;
    if (prot & PROT_WRITE) pte_prot |= PTE_WRITABLE;
    if (!(prot & PROT_EXEC)) pte_prot |= PTE_NX;

    /* Create the VMA - no physical pages allocated yet */
    vma_t *vma = vma_create(proc->as->vmas, map_addr, map_addr + length,
                            vma_flags, pte_prot);
    if (!vma) {
        ERROR("sys_mmap: failed to create VMA");
        if (file_node) vfs_node_unref(file_node);
        return -ENOMEM;
    }

    /* Set file backing if this is a file mapping */
    if (file_node) {
        vma->file = file_node;
        vma->file_offset = offset;
    }

    DEBUG("sys_mmap: created VMA 0x%llx-0x%llx (lazy)", map_addr, map_addr + length);
    return (int64_t)map_addr;
}

/*
 * sys_munmap - unmap memory
 */
static int64_t sys_munmap(uint64_t addr, uint64_t length, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (addr & (PAGE_SIZE - 1)) {
        return -EINVAL;
    }
    if (length == 0) {
        return -EINVAL;
    }

    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t num_pages = length / PAGE_SIZE;

    process_t *proc = process_current();

    /* Unmap physical pages if they exist */
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t page_addr = addr + i * PAGE_SIZE;
        /* Use as_free_page if we have an address space, otherwise vmm */
        if (proc && proc->as) {
            as_free_page(proc->as, page_addr);
        } else {
            vmm_unmap_page(page_addr);
        }
    }

    /* Remove VMA if we have one */
    if (proc && proc->as && proc->as->vmas) {
        vma_t *vma = vma_find(proc->as->vmas, addr);
        if (vma) {
            /* For simplicity, remove the entire VMA if it matches */
            /* TODO: handle partial unmaps by splitting VMAs */
            if (vma->start == addr && vma->end == addr + length) {
                vma_destroy(proc->as->vmas, vma);
            }
        }
    }

    return 0;
}

/*
 * sys_mprotect - change memory protection
 */
static int64_t sys_mprotect(uint64_t addr, uint64_t length, uint64_t prot,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    if (addr & (PAGE_SIZE - 1)) {
        return -EINVAL;
    }
    if (length == 0) {
        return 0;
    }

    /* For now, just validate the request - full implementation would
     * modify PTEs to change protection */
    (void)prot;

    return 0;
}

/*
 * sys_msync - synchronize a file mapping
 */
static int64_t sys_msync(uint64_t addr, uint64_t length, uint64_t flags,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    if (addr & (PAGE_SIZE - 1)) {
        return -EINVAL;
    }
    if (length == 0) {
        return 0;
    }

    process_t *proc = process_current();
    if (!proc || !proc->as || !proc->as->vmas) {
        return -ENOMEM;
    }

    /* Find VMA for this address */
    vma_t *vma = vma_find(proc->as->vmas, addr);
    if (!vma) {
        return -ENOMEM;
    }

    /* Only file-backed VMAs can be synced */
    vfs_node_t *file = (vfs_node_t *)vma->file;
    if (!file) {
        return 0;  /* Anonymous mapping - nothing to sync */
    }

    /* MS_SYNC = synchronous, MS_ASYNC = schedule write */
    /* For now we just do synchronous writes */
    (void)flags;

    /* Sync pages in this range through page cache */
    page_cache_sync(file);

    return 0;
}

/* ============================================================================
 * NEW SYSCALLS - Time
 * ============================================================================ */

/* Boot time in seconds since epoch (placeholder) */
/*
 * Get current time with microsecond precision using HPET or PIT
 */
static void get_current_time(uint64_t *sec, uint64_t *usec) {
    uint64_t boot_time = rtc_get_boot_time();

    /* Use HPET for better precision if available */
    if (hpet_is_available()) {
        uint64_t freq = hpet_get_frequency();
        uint64_t ticks = hpet_read_counter();
        uint64_t secs = ticks / freq;
        uint64_t remainder = ticks % freq;
        *sec = boot_time + secs;
        *usec = (remainder * 1000000) / freq;
    } else {
        /* Fall back to PIT */
        uint64_t uptime_ms = pit_get_uptime_ms();
        *sec = boot_time + uptime_ms / 1000;
        *usec = (uptime_ms % 1000) * 1000;
    }
}

/*
 * sys_gettimeofday - get current time
 */
static int64_t sys_gettimeofday(uint64_t tv, uint64_t tz, uint64_t arg3,
                                uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (tv) {
        if (!access_ok((void *)tv, sizeof(timeval_t))) {
            return -EFAULT;
        }

        timeval_t ktv;
        uint64_t usec;
        get_current_time((uint64_t *)&ktv.tv_sec, &usec);
        ktv.tv_usec = (long)usec;

        if (copy_to_user((void *)tv, &ktv, sizeof(ktv)) < 0) {
            return -EFAULT;
        }
    }

    if (tz) {
        if (!access_ok((void *)tz, sizeof(timezone_t))) {
            return -EFAULT;
        }

        timezone_t ktz = { 0, 0 };
        if (copy_to_user((void *)tz, &ktz, sizeof(ktz)) < 0) {
            return -EFAULT;
        }
    }

    return 0;
}

/*
 * sys_clock_gettime - get time from a clock
 */
static int64_t sys_clock_gettime(uint64_t clockid, uint64_t tp, uint64_t arg3,
                                 uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (!access_ok((void *)tp, sizeof(timespec_t))) {
        return -EFAULT;
    }

    timespec_t kts;
    uint64_t sec, usec;
    uint64_t uptime_ms = pit_get_uptime_ms();

    switch ((int)clockid) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        get_current_time(&sec, &usec);
        kts.tv_sec = sec;
        kts.tv_nsec = usec * 1000;
        break;

    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_MONOTONIC_COARSE:
        kts.tv_sec = uptime_ms / 1000;
        kts.tv_nsec = (uptime_ms % 1000) * 1000000;
        break;

    case CLOCK_PROCESS_CPUTIME:
    case CLOCK_THREAD_CPUTIME:
        /* Return uptime as process/thread time for simplicity */
        kts.tv_sec = uptime_ms / 1000;
        kts.tv_nsec = (uptime_ms % 1000) * 1000000;
        break;

    default:
        return -EINVAL;
    }

    if (copy_to_user((void *)tp, &kts, sizeof(kts)) < 0) {
        return -EFAULT;
    }

    return 0;
}

/*
 * sys_clock_getres - get clock resolution
 */
static int64_t sys_clock_getres(uint64_t clockid, uint64_t res, uint64_t arg3,
                                uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (!res) {
        return 0;
    }

    if (!access_ok((void *)res, sizeof(timespec_t))) {
        return -EFAULT;
    }

    /* All clocks have 1ms resolution (PIT-based) */
    timespec_t kts;
    kts.tv_sec = 0;
    kts.tv_nsec = 1000000;  /* 1ms in nanoseconds */

    switch ((int)clockid) {
    case CLOCK_REALTIME:
    case CLOCK_MONOTONIC:
    case CLOCK_PROCESS_CPUTIME:
    case CLOCK_THREAD_CPUTIME:
    case CLOCK_REALTIME_COARSE:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_MONOTONIC_RAW:
        break;
    default:
        return -EINVAL;
    }

    if (copy_to_user((void *)res, &kts, sizeof(kts)) < 0) {
        return -EFAULT;
    }

    return 0;
}

/*
 * sys_getrandom - obtain random bytes
 */
static int64_t sys_getrandom(uint64_t buf, uint64_t buflen, uint64_t flags,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    if (!buf || buflen == 0) {
        return 0;
    }

    /* Validate buffer */
    if (!access_ok((void *)buf, buflen)) {
        return -EFAULT;
    }

    /* We don't support GRND_RANDOM (blocking /dev/random) */
    /* Just use our PRNG for all requests */
    (void)flags;

    /* Generate random bytes */
    extern void get_random_bytes(void *buf, size_t size);
    get_random_bytes((void *)buf, buflen);

    return (int64_t)buflen;
}

/*
 * sys_nanosleep - sleep for specified time
 */
static int64_t sys_nanosleep(uint64_t req, uint64_t rem, uint64_t arg3,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (!access_ok((void *)req, sizeof(timespec_t))) {
        return -EFAULT;
    }

    timespec_t kreq;
    if (copy_from_user(&kreq, (void *)req, sizeof(kreq)) < 0) {
        return -EFAULT;
    }

    if (kreq.tv_sec < 0 || kreq.tv_nsec < 0 || kreq.tv_nsec >= 1000000000) {
        return -EINVAL;
    }

    /* Convert to milliseconds and sleep */
    uint64_t ms = kreq.tv_sec * 1000 + kreq.tv_nsec / 1000000;
    if (ms > 0) {
        thread_sleep_ms(ms);
    }

    /* Set remaining time to 0 */
    if (rem) {
        if (!access_ok((void *)rem, sizeof(timespec_t))) {
            return -EFAULT;
        }
        timespec_t krem = { 0, 0 };
        if (copy_to_user((void *)rem, &krem, sizeof(krem)) < 0) {
            return -EFAULT;
        }
    }

    return 0;
}

/*
 * sys_sched_yield - yield the processor
 */
static int64_t sys_sched_yield(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                               uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    sched_reschedule();
    return 0;
}

/* ============================================================================
 * NEW SYSCALLS - Process/User Identity
 * ============================================================================ */

/*
 * sys_getuid/geteuid/getgid/getegid - get user/group IDs
 */
static int64_t sys_getuid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    return proc ? (int64_t)proc->uid : 0;
}

static int64_t sys_geteuid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    return proc ? (int64_t)proc->euid : 0;
}

static int64_t sys_getgid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    return proc ? (int64_t)proc->gid : 0;
}

static int64_t sys_getegid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    return proc ? (int64_t)proc->egid : 0;
}

/*
 * sys_setuid - set user ID
 * POSIX semantics: if euid==0 (root), set all IDs; otherwise set only euid if permitted
 */
static int64_t sys_setuid(uint64_t uid, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    /* Root can set all IDs */
    if (proc->euid == 0) {
        proc->uid = proc->euid = proc->suid = (uint32_t)uid;
        return 0;
    }

    /* Non-root can only set euid to uid or suid */
    if (uid == proc->uid || uid == proc->suid) {
        proc->euid = (uint32_t)uid;
        return 0;
    }

    return -EPERM;
}

/*
 * sys_setgid - set group ID
 */
static int64_t sys_setgid(uint64_t gid, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    /* Root can set all IDs */
    if (proc->euid == 0) {
        proc->gid = proc->egid = proc->sgid = (uint32_t)gid;
        return 0;
    }

    /* Non-root can only set egid to gid or sgid */
    if (gid == proc->gid || gid == proc->sgid) {
        proc->egid = (uint32_t)gid;
        return 0;
    }

    return -EPERM;
}

/*
 * sys_setreuid - set real and effective user ID
 */
static int64_t sys_setreuid(uint64_t ruid, uint64_t euid, uint64_t arg3,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    uint32_t new_ruid = (ruid == (uint64_t)-1) ? proc->uid : (uint32_t)ruid;
    uint32_t new_euid = (euid == (uint64_t)-1) ? proc->euid : (uint32_t)euid;

    /* Check permissions */
    if (proc->euid != 0) {
        /* Non-root: can only set to current values */
        if ((new_ruid != proc->uid && new_ruid != proc->euid) ||
            (new_euid != proc->uid && new_euid != proc->euid && new_euid != proc->suid)) {
            return -EPERM;
        }
    }

    /* Set saved UID if we're changing real UID or if euid != old euid */
    if (new_ruid != proc->uid || (proc->euid != 0 && new_euid != proc->euid)) {
        proc->suid = new_euid;
    }

    proc->uid = new_ruid;
    proc->euid = new_euid;
    return 0;
}

/*
 * sys_setregid - set real and effective group ID
 */
static int64_t sys_setregid(uint64_t rgid, uint64_t egid, uint64_t arg3,
                            uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    uint32_t new_rgid = (rgid == (uint64_t)-1) ? proc->gid : (uint32_t)rgid;
    uint32_t new_egid = (egid == (uint64_t)-1) ? proc->egid : (uint32_t)egid;

    /* Check permissions */
    if (proc->euid != 0) {
        if ((new_rgid != proc->gid && new_rgid != proc->egid) ||
            (new_egid != proc->gid && new_egid != proc->egid && new_egid != proc->sgid)) {
            return -EPERM;
        }
    }

    if (new_rgid != proc->gid || (proc->euid != 0 && new_egid != proc->egid)) {
        proc->sgid = new_egid;
    }

    proc->gid = new_rgid;
    proc->egid = new_egid;
    return 0;
}

/*
 * sys_setresuid - set real, effective, and saved user ID
 */
static int64_t sys_setresuid(uint64_t ruid, uint64_t euid, uint64_t suid,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    uint32_t new_ruid = (ruid == (uint64_t)-1) ? proc->uid : (uint32_t)ruid;
    uint32_t new_euid = (euid == (uint64_t)-1) ? proc->euid : (uint32_t)euid;
    uint32_t new_suid = (suid == (uint64_t)-1) ? proc->suid : (uint32_t)suid;

    /* Check permissions */
    if (proc->euid != 0) {
        if ((new_ruid != proc->uid && new_ruid != proc->euid && new_ruid != proc->suid) ||
            (new_euid != proc->uid && new_euid != proc->euid && new_euid != proc->suid) ||
            (new_suid != proc->uid && new_suid != proc->euid && new_suid != proc->suid)) {
            return -EPERM;
        }
    }

    proc->uid = new_ruid;
    proc->euid = new_euid;
    proc->suid = new_suid;
    return 0;
}

/*
 * sys_setresgid - set real, effective, and saved group ID
 */
static int64_t sys_setresgid(uint64_t rgid, uint64_t egid, uint64_t sgid,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    uint32_t new_rgid = (rgid == (uint64_t)-1) ? proc->gid : (uint32_t)rgid;
    uint32_t new_egid = (egid == (uint64_t)-1) ? proc->egid : (uint32_t)egid;
    uint32_t new_sgid = (sgid == (uint64_t)-1) ? proc->sgid : (uint32_t)sgid;

    /* Check permissions */
    if (proc->euid != 0) {
        if ((new_rgid != proc->gid && new_rgid != proc->egid && new_rgid != proc->sgid) ||
            (new_egid != proc->gid && new_egid != proc->egid && new_egid != proc->sgid) ||
            (new_sgid != proc->gid && new_sgid != proc->egid && new_sgid != proc->sgid)) {
            return -EPERM;
        }
    }

    proc->gid = new_rgid;
    proc->egid = new_egid;
    proc->sgid = new_sgid;
    return 0;
}

/*
 * sys_getresuid - get real, effective, and saved user ID
 */
static int64_t sys_getresuid(uint64_t ruid_ptr, uint64_t euid_ptr, uint64_t suid_ptr,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    uint32_t *ruid = (uint32_t *)ruid_ptr;
    uint32_t *euid = (uint32_t *)euid_ptr;
    uint32_t *suid = (uint32_t *)suid_ptr;

    if (ruid) *ruid = proc->uid;
    if (euid) *euid = proc->euid;
    if (suid) *suid = proc->suid;
    return 0;
}

/*
 * sys_getresgid - get real, effective, and saved group ID
 */
static int64_t sys_getresgid(uint64_t rgid_ptr, uint64_t egid_ptr, uint64_t sgid_ptr,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (!proc) return -ESRCH;

    uint32_t *rgid = (uint32_t *)rgid_ptr;
    uint32_t *egid = (uint32_t *)egid_ptr;
    uint32_t *sgid = (uint32_t *)sgid_ptr;

    if (rgid) *rgid = proc->gid;
    if (egid) *egid = proc->egid;
    if (sgid) *sgid = proc->sgid;
    return 0;
}

/*
 * sys_setsid - create a new session
 */
static int64_t sys_setsid(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    process_t *proc = process_current();
    if (proc) {
        /* Make this process a session leader */
        proc->session_id = proc->pid;
        proc->pgrp = proc->pid;
        return (int64_t)proc->pid;
    }
    return -EPERM;
}

/*
 * sys_getpgid - get process group ID
 */
static int64_t sys_getpgid(uint64_t pid, uint64_t arg2, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (pid == 0) {
        process_t *proc = process_current();
        return proc ? (int64_t)proc->pgrp : 0;
    }

    process_t *target = process_get_by_pid((uint32_t)pid);
    if (!target) {
        return -ESRCH;
    }
    return (int64_t)target->pgrp;
}

/*
 * sys_setpgid - set process group ID
 */
static int64_t sys_setpgid(uint64_t pid, uint64_t pgid, uint64_t arg3,
                           uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc) {
        return -ESRCH;
    }

    uint32_t target_pid = pid ? (uint32_t)pid : proc->pid;
    uint32_t new_pgid = pgid ? (uint32_t)pgid : target_pid;

    /* Validate the setpgid operation */
    extern int pgrp_validate_setpgid(uint32_t target_pid, uint32_t new_pgrp);
    int ret = pgrp_validate_setpgid(target_pid, new_pgid);
    if (ret < 0) {
        return ret;
    }

    process_t *target = process_get_by_pid(target_pid);
    if (!target) {
        return -ESRCH;
    }

    target->pgrp = new_pgid;
    DEBUG("sys_setpgid: PID %u set to pgrp %u", target_pid, new_pgid);
    return 0;
}

/*
 * sys_getsid - get session ID
 */
static int64_t sys_getsid(uint64_t pid, uint64_t arg2, uint64_t arg3,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (pid == 0) {
        process_t *proc = process_current();
        return proc ? (int64_t)proc->session_id : 0;
    }

    process_t *target = process_get_by_pid((uint32_t)pid);
    if (!target) {
        return -ESRCH;
    }
    return (int64_t)target->session_id;
}

/* ============================================================================
 * NEW SYSCALLS - Signals
 * ============================================================================ */

/*
 * sys_sigaction - set signal handler
 */
static int64_t sys_sigaction(uint64_t signum, uint64_t act, uint64_t oldact,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    if (signum >= NSIG || signum == 0) {
        return -EINVAL;
    }

    process_t *proc = process_current();
    if (!proc || !proc->signals) {
        return -ESRCH;
    }

    /* Get old action if requested */
    if (oldact) {
        if (!access_ok((void *)oldact, sizeof(sigaction_t))) {
            return -EFAULT;
        }
        if (copy_to_user((void *)oldact, &proc->signals->actions[signum],
                         sizeof(sigaction_t)) < 0) {
            return -EFAULT;
        }
    }

    /* Set new action if provided */
    if (act) {
        if (!access_ok((void *)act, sizeof(sigaction_t))) {
            return -EFAULT;
        }
        if (copy_from_user(&proc->signals->actions[signum], (void *)act,
                           sizeof(sigaction_t)) < 0) {
            return -EFAULT;
        }
    }

    return 0;
}

/*
 * sys_sigprocmask - manipulate signal mask
 */
static int64_t sys_sigprocmask(uint64_t how, uint64_t set, uint64_t oldset,
                               uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    process_t *proc = process_current();
    if (!proc || !proc->signals) {
        return -ESRCH;
    }

    /* Return old mask if requested */
    if (oldset) {
        if (!access_ok((void *)oldset, sizeof(sigset_t))) {
            return -EFAULT;
        }
        if (copy_to_user((void *)oldset, &proc->signals->blocked,
                         sizeof(sigset_t)) < 0) {
            return -EFAULT;
        }
    }

    /* Modify mask if new set provided */
    if (set) {
        if (!access_ok((void *)set, sizeof(sigset_t))) {
            return -EFAULT;
        }

        sigset_t kset;
        if (copy_from_user(&kset, (void *)set, sizeof(kset)) < 0) {
            return -EFAULT;
        }

        switch ((int)how) {
        case 0:  /* SIG_BLOCK */
            proc->signals->blocked |= kset;
            break;
        case 1:  /* SIG_UNBLOCK */
            proc->signals->blocked &= ~kset;
            break;
        case 2:  /* SIG_SETMASK */
            proc->signals->blocked = kset;
            break;
        default:
            return -EINVAL;
        }

        /* SIGKILL and SIGSTOP cannot be blocked */
        proc->signals->blocked &= ~((1UL << SIGKILL) | (1UL << SIGSTOP));
    }

    return 0;
}

/*
 * sys_sigreturn_impl - return from signal handler
 *
 * Restores the context from the signal frame on the user stack.
 * This is the inverse of what signal_deliver_pending() does.
 *
 * @param frame  Syscall frame to restore into
 * @return Does not return normally - modifies frame to restore original context
 */
static int64_t sys_sigreturn_impl(syscall_frame_t *frame) {
    process_t *proc = process_current();
    if (!proc || !proc->signals) {
        return -ESRCH;
    }

    /*
     * The signal frame is on the user stack. When the trampoline called
     * sigreturn, RSP pointed just above the trampoline code, which is
     * at the end of signal_frame_t.
     *
     * We need to find the signal_frame_t base by subtracting from current RSP.
     * The user called "syscall" from the trampoline, so RSP is what it was
     * when they called syscall.
     */

    /* The trampoline is at the end of signal_frame_t */
    /* When sigreturn is called, RSP points to just below where the
     * "ret" would have returned (i.e., at the start of trampoline code) */
    uint64_t user_rsp = frame->rsp;

    /* Find the signal frame - it's at (rsp - sizeof(trampoline)) rounded down */
    uint64_t frame_addr = user_rsp - offsetof(signal_frame_t, trampoline);

    DEBUG("Signal: sigreturn from RSP 0x%llx, frame at 0x%llx",
          (unsigned long long)user_rsp, (unsigned long long)frame_addr);

    /* Read the signal frame from user stack */
    /* Note: In production, use copy_from_user with proper checks */
    signal_frame_t *sig_frame = (signal_frame_t *)frame_addr;

    /* Validate frame address */
    if (frame_addr < 0x1000 || frame_addr >= 0x00007FFFFFFFFFFF) {
        WARN("Signal: sigreturn with invalid frame address 0x%llx", frame_addr);
        return -EFAULT;
    }

    /* Restore registers from signal frame */
    frame->r15 = sig_frame->r15;
    frame->r14 = sig_frame->r14;
    frame->r13 = sig_frame->r13;
    frame->r12 = sig_frame->r12;
    frame->rbp = sig_frame->rbp;
    frame->rbx = sig_frame->rbx;
    frame->r9 = sig_frame->r9;
    frame->r8 = sig_frame->r8;
    frame->r10 = sig_frame->r10;
    frame->rdx = sig_frame->rdx;
    frame->rsi = sig_frame->rsi;
    frame->rdi = sig_frame->rdi;
    frame->rax = sig_frame->rax;     /* Original return value */
    frame->rcx = sig_frame->rcx;     /* Original RIP */
    frame->r11 = sig_frame->r11;     /* Original RFLAGS */
    frame->rsp = sig_frame->rsp;     /* Original RSP */

    /* Restore blocked signal mask */
    proc->signals->blocked = sig_frame->saved_mask;

    DEBUG("Signal: sigreturn restoring RIP=0x%llx, RSP=0x%llx, RAX=0x%llx",
          (unsigned long long)frame->rcx, (unsigned long long)frame->rsp,
          (unsigned long long)frame->rax);

    /* Return the original syscall return value */
    return (int64_t)frame->rax;
}

/*
 * sys_sigreturn - stub for syscall table (actual work done in dispatch)
 */
static int64_t sys_sigreturn(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                             uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    /* This should never be called - handled specially in syscall_dispatch */
    return -ENOSYS;
}

/* ============================================================================
 * NEW SYSCALLS - Miscellaneous
 * ============================================================================ */

/*
 * sys_uname - get system information
 */
static int64_t sys_uname(uint64_t buf, uint64_t arg2, uint64_t arg3,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (!access_ok((void *)buf, sizeof(utsname_t))) {
        return -EFAULT;
    }

    utsname_t kname;
    strncpy(kname.sysname, "Kurios2", UTSNAME_LENGTH);
    strncpy(kname.nodename, "kurios", UTSNAME_LENGTH);
    strncpy(kname.release, "0.1.0", UTSNAME_LENGTH);
    strncpy(kname.version, "0.1.0 " __DATE__, UTSNAME_LENGTH);
    strncpy(kname.machine, "x86_64", UTSNAME_LENGTH);
    strncpy(kname.domainname, "(none)", UTSNAME_LENGTH);

    if (copy_to_user((void *)buf, &kname, sizeof(kname)) < 0) {
        return -EFAULT;
    }

    return 0;
}

/*
 * Check if file descriptor is a TTY
 */
static bool is_tty(int fd) {
    process_t *proc = process_current();
    if (!proc || !proc->fd_table) {
        return false;
    }

    file_t *file = fd_table_get(proc->fd_table, fd);
    if (!file || !file->node) {
        return false;
    }

    /* Check if it's /dev/console (a chardev named "console") */
    if (file->node->type == VFS_CHARDEV &&
        strcmp(file->node->name, "console") == 0) {
        return true;
    }

    return false;
}

/*
 * sys_ioctl - device control
 */
static int64_t sys_ioctl(uint64_t fd, uint64_t request, uint64_t arg,
                         uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;

    /* Check if fd is a TTY */
    bool tty = is_tty((int)fd);

    switch (request) {
    case 0x5401:  /* TCGETS - get terminal attributes */
        if (!tty) {
            return -ENOTTY;
        }
        /* For now, just return success (isatty() only checks return value) */
        return 0;

    case 0x5413:  /* TIOCGWINSZ - get window size */
        if (!tty) {
            return -ENOTTY;
        }
        /* Return default 80x25 window size */
        if (arg && access_ok((void *)arg, 8)) {
            uint16_t *ws = (uint16_t *)arg;
            ws[0] = 25;   /* ws_row */
            ws[1] = 80;   /* ws_col */
            ws[2] = 0;    /* ws_xpixel */
            ws[3] = 0;    /* ws_ypixel */
            return 0;
        }
        return -EFAULT;

    default:
        return -ENOTTY;
    }
}

/*
 * sys_pipe - create a pipe
 */
static int64_t sys_pipe(uint64_t pipefd, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;

    if (!access_ok((void *)pipefd, 2 * sizeof(int))) {
        return -EFAULT;
    }

    int read_fd, write_fd;
    int result = pipe_create(&read_fd, &write_fd);
    if (result < 0) {
        return result;
    }

    /* Copy file descriptors to user space */
    int *user_fds = (int *)pipefd;
    user_fds[0] = read_fd;
    user_fds[1] = write_fd;

    return 0;
}

/*
 * sys_syslog - read/control kernel log (stub)
 */
static int64_t sys_syslog(uint64_t type, uint64_t bufp, uint64_t len,
                          uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)bufp; (void)len; (void)arg4; (void)arg5; (void)arg6;

    switch ((int)type) {
    case 0:  /* SYSLOG_ACTION_CLOSE */
    case 1:  /* SYSLOG_ACTION_OPEN */
        return 0;
    case 10: /* SYSLOG_ACTION_SIZE_BUFFER */
        return 16384;  /* Return a reasonable buffer size */
    default:
        return -EINVAL;
    }
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
     * Use parent->as if available, otherwise create a temporary one
     */
    address_space_t *parent_as = parent->as;
    address_space_t temp_as;
    if (!parent_as) {
        /* Fallback for processes without as struct */
        temp_as.cr3 = parent->cr3;
        temp_as.ref_count = 1;
        temp_as.user_pages = 0;
        temp_as.vmas = NULL;
        parent_as = &temp_as;
    }

    address_space_t *child_as = as_clone(parent_as);
    if (!child_as) {
        ERROR("sys_fork: failed to clone address space");
        process_exit(child, -1);
        process_destroy(child);
        return -ENOMEM;
    }

    /* Destroy the address space that process_create made */
    if (child->as) {
        as_destroy(child->as);
    }

    /* Link cloned address space to child */
    child->as = child_as;
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

    /* Check execute permission */
    /* TODO: Proper UID/GID checking when credential system is implemented */
    if (!(st.permissions & VFS_PERM_EXEC)) {
        vfs_close(fd);
        ERROR("sys_execve: '%s' is not executable (permissions: 0x%x)", path, st.permissions);
        return -EACCES;
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
        int64_t result = sys_fork_impl(frame);
        /* Check for pending signals before returning to user mode */
        signal_deliver_pending(frame);
        return result;
    }
    if (syscall_num == SYS_EXECVE) {
        int64_t result = sys_execve_impl(frame);
        /* Check for pending signals before returning to user mode */
        signal_deliver_pending(frame);
        return result;
    }
    if (syscall_num == SYS_SIGRETURN) {
        /* sigreturn restores context from signal frame - no signal check after */
        return sys_sigreturn_impl(frame);
    }

    syscall_handler_t handler = syscall_table[syscall_num];
    if (!handler) {
        handler = sys_unimplemented;
    }

    int64_t result = handler(frame->rdi, frame->rsi, frame->rdx,
                             frame->r10, frame->r8, frame->r9);

    /* Check for pending signals before returning to user mode */
    signal_deliver_pending(frame);

    return result;
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
    syscall_register(SYS_READ, sys_read);
    syscall_register(SYS_WRITE, sys_write);
    syscall_register(SYS_OPEN, sys_open);
    syscall_register(SYS_CLOSE, sys_close);
    syscall_register(SYS_STAT, sys_stat);
    syscall_register(SYS_FSTAT, sys_fstat);
    syscall_register(SYS_LSTAT, sys_lstat);
    syscall_register(SYS_LSEEK, sys_lseek);
    syscall_register(SYS_MMAP, sys_mmap);
    syscall_register(SYS_MPROTECT, sys_mprotect);
    syscall_register(SYS_MUNMAP, sys_munmap);
    syscall_register(SYS_MSYNC, sys_msync);
    syscall_register(SYS_BRK, sys_brk);
    syscall_register(SYS_SIGACTION, sys_sigaction);
    syscall_register(SYS_SIGPROCMASK, sys_sigprocmask);
    syscall_register(SYS_SIGRETURN, sys_sigreturn);
    syscall_register(SYS_IOCTL, sys_ioctl);
    syscall_register(SYS_ACCESS, sys_access);
    syscall_register(SYS_PIPE, sys_pipe);
    syscall_register(SYS_SCHED_YIELD, sys_sched_yield);
    syscall_register(SYS_DUP, sys_dup);
    syscall_register(SYS_DUP2, sys_dup2);
    syscall_register(SYS_FCNTL, sys_fcntl);
    syscall_register(SYS_NANOSLEEP, sys_nanosleep);
    syscall_register(SYS_ALARM, sys_alarm);
    syscall_register(SYS_EXIT, sys_exit);
    syscall_register(SYS_GETPID, sys_getpid);
    syscall_register(SYS_GETPPID, sys_getppid);
    syscall_register(SYS_WAIT4, sys_wait4);
    syscall_register(SYS_KILL, sys_kill);
    syscall_register(SYS_UNAME, sys_uname);
    syscall_register(SYS_TRUNCATE, sys_truncate);
    syscall_register(SYS_FTRUNCATE, sys_ftruncate);
    syscall_register(SYS_GETDENTS, sys_getdents);
    syscall_register(SYS_GETCWD, sys_getcwd);
    syscall_register(SYS_CHDIR, sys_chdir);
    syscall_register(SYS_FCHDIR, sys_fchdir);
    syscall_register(SYS_RENAME, sys_rename);
    syscall_register(SYS_MKDIR, sys_mkdir);
    syscall_register(SYS_MKNOD, sys_mknod);
    syscall_register(SYS_RMDIR, sys_rmdir);
    syscall_register(SYS_UNLINK, sys_unlink);
    syscall_register(SYS_READLINK, sys_readlink);
    syscall_register(SYS_UMASK, sys_umask);
    syscall_register(SYS_GETTIMEOFDAY, sys_gettimeofday);
    syscall_register(SYS_GETUID, sys_getuid);
    syscall_register(SYS_SYSLOG, sys_syslog);
    syscall_register(SYS_GETGID, sys_getgid);
    syscall_register(SYS_SETUID, sys_setuid);
    syscall_register(SYS_SETGID, sys_setgid);
    syscall_register(SYS_GETEUID, sys_geteuid);
    syscall_register(SYS_GETEGID, sys_getegid);
    syscall_register(SYS_SETREUID, sys_setreuid);
    syscall_register(SYS_SETREGID, sys_setregid);
    syscall_register(SYS_SETRESUID, sys_setresuid);
    syscall_register(SYS_SETRESGID, sys_setresgid);
    syscall_register(SYS_GETRESUID, sys_getresuid);
    syscall_register(SYS_GETRESGID, sys_getresgid);
    syscall_register(SYS_SETSID, sys_setsid);
    syscall_register(SYS_GETPGID, sys_getpgid);
    syscall_register(SYS_SETPGID, sys_setpgid);
    syscall_register(SYS_GETSID, sys_getsid);
    syscall_register(SYS_CLOCK_GETTIME, sys_clock_gettime);
    syscall_register(SYS_CLOCK_GETRES, sys_clock_getres);
    syscall_register(SYS_GETRANDOM, sys_getrandom);

    /* File permission and ownership syscalls */
    syscall_register(SYS_CHMOD, sys_chmod);
    syscall_register(SYS_FCHMOD, sys_fchmod);
    syscall_register(SYS_CHOWN, sys_chown);
    syscall_register(SYS_FCHOWN, sys_fchown);
    syscall_register(SYS_LCHOWN, sys_lchown);

    /* Filesystem sync syscalls */
    syscall_register(SYS_SYNC, sys_sync);
    syscall_register(SYS_FSYNC, sys_fsync);
    syscall_register(SYS_FDATASYNC, sys_fdatasync);

    /* Socket syscalls - stub implementations returning ENOSYS */
    syscall_register(SYS_SOCKET, sys_unimplemented);
    syscall_register(SYS_CONNECT, sys_unimplemented);
    syscall_register(SYS_ACCEPT, sys_unimplemented);
    syscall_register(SYS_SENDTO, sys_unimplemented);
    syscall_register(SYS_RECVFROM, sys_unimplemented);
    syscall_register(SYS_BIND, sys_unimplemented);
    syscall_register(SYS_LISTEN, sys_unimplemented);

    /* Resource usage and time syscalls */
    syscall_register(SYS_GETRUSAGE, sys_getrusage);
    syscall_register(SYS_TIMES, sys_times);

    /* Resource limit syscalls - stub implementations returning ENOSYS */
    syscall_register(SYS_GETRLIMIT, sys_unimplemented);
    syscall_register(SYS_SETRLIMIT, sys_unimplemented);

    /* Filesystem link syscalls */
    syscall_register(SYS_LINK, sys_link);
    syscall_register(SYS_SYMLINK, sys_symlink);

    /* I/O multiplexing syscalls - stub implementations returning ENOSYS */
    syscall_register(SYS_POLL, sys_unimplemented);
    syscall_register(SYS_SELECT, sys_unimplemented);

    /* Note: SYS_FORK and SYS_EXECVE are handled specially in syscall_dispatch */

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
    kprintf("\n=== Syscall Tests ===\n");
    syscall_frame_t frame = {0};
    int64_t result;
    int passed = 0, failed = 0;

    /*
     * Enable kernel testing mode so kernel addresses pass access_ok().
     * This allows us to test syscalls that expect user-space pointers
     * using kernel-space buffers.
     */
    uaccess_set_kernel_testing(true);

    /* Create /tmp directory for filesystem tests */
    int tmp_ret = vfs_mkdir("/tmp");
    if (tmp_ret != VFS_OK && tmp_ret != -EEXIST) {
        kprintf("  Warning: Could not create /tmp (%d)\n", tmp_ret);
    }

    /* Infrastructure tests */
    kprintf("--- Infrastructure ---\n");
    kprintf("  Initialized: %s\n", syscall_initialized ? "OK" : "FAIL");
    if (syscall_initialized) passed++; else failed++;

    uint64_t efer = rdmsr(MSR_EFER);
    bool efer_ok = (efer & EFER_SCE) != 0;
    kprintf("  EFER.SCE: %s\n", efer_ok ? "OK" : "FAIL");
    if (efer_ok) passed++; else failed++;

    /* Test getpid */
    kprintf("--- Process/Identity ---\n");
    frame.rax = SYS_GETPID;
    result = syscall_dispatch(&frame);
    kprintf("  getpid: %s (pid=%lld)\n", result >= 0 ? "OK" : "FAIL", result);
    if (result >= 0) passed++; else failed++;

    /* Test getppid */
    frame.rax = SYS_GETPPID;
    result = syscall_dispatch(&frame);
    kprintf("  getppid: %s (ppid=%lld)\n", result >= 0 ? "OK" : "FAIL", result);
    if (result >= 0) passed++; else failed++;

    /* Test getuid */
    frame.rax = SYS_GETUID;
    result = syscall_dispatch(&frame);
    kprintf("  getuid: %s (uid=%lld)\n", result == 0 ? "OK" : "FAIL", result);
    if (result == 0) passed++; else failed++;

    /* Test geteuid */
    frame.rax = SYS_GETEUID;
    result = syscall_dispatch(&frame);
    kprintf("  geteuid: %s (euid=%lld)\n", result == 0 ? "OK" : "FAIL", result);
    if (result == 0) passed++; else failed++;

    /* Test getgid */
    frame.rax = SYS_GETGID;
    result = syscall_dispatch(&frame);
    kprintf("  getgid: %s (gid=%lld)\n", result == 0 ? "OK" : "FAIL", result);
    if (result == 0) passed++; else failed++;

    /* Test getegid */
    frame.rax = SYS_GETEGID;
    result = syscall_dispatch(&frame);
    kprintf("  getegid: %s (egid=%lld)\n", result == 0 ? "OK" : "FAIL", result);
    if (result == 0) passed++; else failed++;

    /* Test setsid */
    frame.rax = SYS_SETSID;
    result = syscall_dispatch(&frame);
    kprintf("  setsid: %s (sid=%lld)\n", result >= 0 || result == -EPERM ? "OK" : "FAIL", result);
    if (result >= 0 || result == -EPERM) passed++; else failed++;

    /* Test getpgid */
    frame.rax = SYS_GETPGID;
    frame.rdi = 0;
    result = syscall_dispatch(&frame);
    kprintf("  getpgid(0): %s (pgid=%lld)\n", result >= 0 ? "OK" : "FAIL", result);
    if (result >= 0) passed++; else failed++;

    /* Test getsid */
    frame.rax = SYS_GETSID;
    frame.rdi = 0;
    result = syscall_dispatch(&frame);
    kprintf("  getsid(0): %s (sid=%lld)\n", result >= 0 ? "OK" : "FAIL", result);
    if (result >= 0) passed++; else failed++;

    /* Test uname */
    kprintf("--- System Info ---\n");
    utsname_t uname_buf;
    frame.rax = SYS_UNAME;
    frame.rdi = (uint64_t)&uname_buf;
    result = syscall_dispatch(&frame);
    kprintf("  uname: %s (sysname=%s)\n", result == 0 ? "OK" : "FAIL",
            result == 0 ? uname_buf.sysname : "?");
    if (result == 0) passed++; else failed++;

    /* Test gettimeofday */
    timeval_t tv;
    frame.rax = SYS_GETTIMEOFDAY;
    frame.rdi = (uint64_t)&tv;
    frame.rsi = 0;
    result = syscall_dispatch(&frame);
    kprintf("  gettimeofday: %s (sec=%lld)\n", result == 0 ? "OK" : "FAIL", tv.tv_sec);
    if (result == 0) passed++; else failed++;

    /* Test clock_gettime */
    timespec_t ts;
    frame.rax = SYS_CLOCK_GETTIME;
    frame.rdi = CLOCK_MONOTONIC;
    frame.rsi = (uint64_t)&ts;
    result = syscall_dispatch(&frame);
    kprintf("  clock_gettime(MONOTONIC): %s (sec=%lld)\n", result == 0 ? "OK" : "FAIL", ts.tv_sec);
    if (result == 0) passed++; else failed++;

    /* Test clock_getres */
    frame.rax = SYS_CLOCK_GETRES;
    frame.rdi = CLOCK_MONOTONIC;
    frame.rsi = (uint64_t)&ts;
    result = syscall_dispatch(&frame);
    kprintf("  clock_getres: %s (nsec=%lld)\n", result == 0 ? "OK" : "FAIL", ts.tv_nsec);
    if (result == 0) passed++; else failed++;

    /* Test brk */
    kprintf("--- Memory ---\n");
    frame.rax = SYS_BRK;
    frame.rdi = 0;
    result = syscall_dispatch(&frame);
    kprintf("  brk(0): %s (brk=0x%llx)\n", result > 0 ? "OK" : "FAIL", result);
    if (result > 0) passed++; else failed++;

    frame.rax = SYS_BRK;
    frame.rdi = 0x10010000;
    result = syscall_dispatch(&frame);
    kprintf("  brk(0x10010000): %s (new_brk=0x%llx)\n", result == 0x10010000 ? "OK" : "FAIL", result);
    if (result == 0x10010000) passed++; else failed++;

    /* Test getcwd */
    kprintf("--- Filesystem ---\n");
    char cwd_buf[256];
    frame.rax = SYS_GETCWD;
    frame.rdi = (uint64_t)cwd_buf;
    frame.rsi = sizeof(cwd_buf);
    result = syscall_dispatch(&frame);
    kprintf("  getcwd: %s (cwd=%s)\n", result != 0 ? "OK" : "FAIL",
            result != 0 ? cwd_buf : "?");
    if (result != 0) passed++; else failed++;

    /* Test chdir */
    const char *dir_path = "/tmp";
    frame.rax = SYS_CHDIR;
    frame.rdi = (uint64_t)dir_path;
    result = syscall_dispatch(&frame);
    kprintf("  chdir(/tmp): %s\n", result == 0 ? "OK" : "FAIL");
    if (result == 0) passed++; else failed++;

    /* Test mkdir */
    const char *new_dir = "/tmp/syscall_test";
    frame.rax = SYS_MKDIR;
    frame.rdi = (uint64_t)new_dir;
    frame.rsi = 0755;
    result = syscall_dispatch(&frame);
    kprintf("  mkdir: %s\n", result == 0 || result == -EEXIST ? "OK" : "FAIL");
    if (result == 0 || result == -EEXIST) passed++; else failed++;

    /* Test access (F_OK) */
    frame.rax = SYS_ACCESS;
    frame.rdi = (uint64_t)"/tmp";
    frame.rsi = F_OK;
    result = syscall_dispatch(&frame);
    kprintf("  access(/tmp, F_OK): %s\n", result == 0 ? "OK" : "FAIL");
    if (result == 0) passed++; else failed++;

    /* Test stat */
    vfs_stat_t stat_buf;
    frame.rax = SYS_STAT;
    frame.rdi = (uint64_t)"/tmp";
    frame.rsi = (uint64_t)&stat_buf;
    result = syscall_dispatch(&frame);
    kprintf("  stat(/tmp): %s (type=%u)\n", result == 0 ? "OK" : "FAIL", stat_buf.type);
    if (result == 0) passed++; else failed++;

    /* Test truncate */
    const char *test_file = "/tmp/test.txt";
    frame.rax = SYS_TRUNCATE;
    frame.rdi = (uint64_t)test_file;
    frame.rsi = 0;
    result = syscall_dispatch(&frame);
    kprintf("  truncate: %s\n", result == 0 || result == -ENOENT ? "OK" : "FAIL");
    if (result == 0 || result == -ENOENT) passed++; else failed++;

    /* Test rmdir */
    frame.rax = SYS_RMDIR;
    frame.rdi = (uint64_t)new_dir;
    result = syscall_dispatch(&frame);
    kprintf("  rmdir: %s\n", result == 0 || result == -ENOENT ? "OK" : "FAIL");
    if (result == 0 || result == -ENOENT) passed++; else failed++;

    /* Test ioctl (expect ENOTTY for stdout) */
    kprintf("--- I/O ---\n");
    frame.rax = SYS_IOCTL;
    frame.rdi = 1;  /* stdout */
    frame.rsi = 0x5401;  /* TCGETS */
    frame.rdx = 0;
    result = syscall_dispatch(&frame);
    kprintf("  ioctl(TCGETS): %s (expected -ENOTTY)\n", result == -ENOTTY ? "OK" : "FAIL");
    if (result == -ENOTTY) passed++; else failed++;

    /* Test sched_yield */
    kprintf("--- Scheduling ---\n");
    frame.rax = SYS_SCHED_YIELD;
    result = syscall_dispatch(&frame);
    kprintf("  sched_yield: %s\n", result == 0 ? "OK" : "FAIL");
    if (result == 0) passed++; else failed++;

    /* Test sigprocmask */
    kprintf("--- Signals ---\n");
    sigset_t old_mask;
    frame.rax = SYS_SIGPROCMASK;
    frame.rdi = 2;  /* SIG_SETMASK */
    frame.rsi = 0;  /* no new mask */
    frame.rdx = (uint64_t)&old_mask;
    result = syscall_dispatch(&frame);
    kprintf("  sigprocmask: %s\n", result == 0 ? "OK" : "FAIL");
    if (result == 0) passed++; else failed++;

    /* Test pipe */
    kprintf("--- Pipes ---\n");
    int pipefd[2];
    frame.rax = SYS_PIPE;
    frame.rdi = (uint64_t)pipefd;
    result = syscall_dispatch(&frame);
    kprintf("  pipe: %s (read=%d, write=%d)\n",
            result == 0 ? "OK" : "FAIL", pipefd[0], pipefd[1]);
    if (result == 0) passed++; else failed++;

    if (result == 0) {
        /* Test pipe write */
        const char *test_msg = "Hello pipe!";
        frame.rax = SYS_WRITE;
        frame.rdi = pipefd[1];
        frame.rsi = (uint64_t)test_msg;
        frame.rdx = 11;
        result = syscall_dispatch(&frame);
        kprintf("  pipe write: %s (wrote %lld bytes)\n",
                result == 11 ? "OK" : "FAIL", result);
        if (result == 11) passed++; else failed++;

        /* Test pipe read */
        char read_buf[32] = {0};
        frame.rax = SYS_READ;
        frame.rdi = pipefd[0];
        frame.rsi = (uint64_t)read_buf;
        frame.rdx = 32;
        result = syscall_dispatch(&frame);
        kprintf("  pipe read: %s (read %lld bytes, got '%s')\n",
                result == 11 ? "OK" : "FAIL", result, read_buf);
        if (result == 11) passed++; else failed++;

        /* Close pipe ends */
        frame.rax = SYS_CLOSE;
        frame.rdi = pipefd[0];
        syscall_dispatch(&frame);
        frame.rdi = pipefd[1];
        syscall_dispatch(&frame);
    }

    /* Test invalid syscall */
    kprintf("--- Error Handling ---\n");
    frame.rax = 999;
    result = syscall_dispatch(&frame);
    kprintf("  invalid syscall: %s (got -ENOSYS)\n", result == -ENOSYS ? "OK" : "FAIL");
    if (result == -ENOSYS) passed++; else failed++;

    /* Disable kernel testing mode */
    uaccess_set_kernel_testing(false);

    /* Summary */
    kprintf("\n=== Syscall Tests: %d passed, %d failed ===\n\n", passed, failed);
}
#endif
