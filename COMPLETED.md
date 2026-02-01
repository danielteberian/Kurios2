# Kurios2 - 64-bit OS Kernel - Completed Work

## Completed Tasks

### Project Setup
- [2026-01-24] Created TODO.md for task tracking
- [2026-01-24] Created COMPLETED.md for tracking completed work

### Toolchain Setup [VERIFIED]
- [2026-01-24] Created `toolchain/config.mk` - Build configuration
- [2026-01-24] Created `toolchain/build-cross-compiler.sh` - Optional cross-compiler build script
- [2026-01-24] Created `toolchain/verify.sh` - Toolchain verification script
- [2026-01-24] Verified: GCC, G++, NASM 2.16.01, LD, Make, QEMU, xorriso, grub-mkrescue

### Custom Hybrid Bootloader [VERIFIED]
- [2026-01-24] BIOS Bootloader with higher-half support
- [2026-01-24] UEFI Bootloader
- [2026-01-24] Common boot protocol (BootInfo structure)

### Debug Framework [VERIFIED]
- [2026-01-24] Serial port driver (`arch/x86_64/serial.c`)
  - COM1 initialization at 115200 baud
  - Blocking putc/getc/puts/write functions
- [2026-01-24] I/O port access (`arch/x86_64/io.h`)
  - inb/outb/inw/outw/inl/outl
  - String I/O functions
- [2026-01-24] CPU utilities (`arch/x86_64/cpu.h`)
  - Control register access (CR0-CR4)
  - MSR read/write
  - CPUID
  - Interrupt control (cli/sti)
  - TLB flush
- [2026-01-24] Debug/logging framework (`debug/debug.c`)
  - kprintf with format specifiers (%d, %x, %s, %p, etc.)
  - Log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
  - ANSI color output for serial
  - ASSERT/ASSERT_MSG macros
  - panic() with stack trace
  - hex_dump() utility
  - Register dump for exceptions

### Memory Management [VERIFIED]
- [2026-01-24] PMM: Buddy allocator (orders 0-10, 4KB-4MB blocks)
  - Page descriptors (page_t) with flags, refcount
  - alloc_pages()/free_pages() with buddy merging
  - pmm_dump_stats()/pmm_dump_free_lists() for debug
- [2026-01-24] VMM: Virtual memory manager
  - 4-level page table walking (PML4/PDPT/PD/PT)
  - Support for 4KB, 2MB, and 1GB pages
  - vmm_map_page()/vmm_unmap_page()
  - vmm_map_pages()/vmm_unmap_pages()
  - vmm_get_phys()/vmm_is_mapped()
  - Auto-allocates page tables on demand
- [2026-01-24] Bootloader: Increased higher-half mapping to 128MB

### Stack Smash Protector [VERIFIED]
- [2026-01-24] Created `stack_protector.c` with canary and fail handler
- [2026-01-24] Randomized canary using TSC, RSP, CR3, function address
- [2026-01-24] Canary has null byte in low position (stops string overflows)
- [2026-01-24] Enabled `-fstack-protector-strong` in toolchain/config.mk
- [2026-01-24] `stack_protector_init()` called early in kernel_main
- [2026-01-24] Test function available in DEBUG_TESTS mode

### Global Constructors/Destructors [VERIFIED]
- [2026-01-24] Added `.init_array` section to linker.ld
- [2026-01-24] Added `.fini_array` section to linker.ld
- [2026-01-24] Constructor calling loop in entry.asm (before kernel_main)
- [2026-01-24] `call_global_destructors()` function exported for shutdown
- [2026-01-24] DEBUG_TESTS conditional compilation for test code
- [2026-01-24] `make debug` / `make run-debug` targets for debug builds

### Multithreading [VERIFIED]
- [2026-01-27] Round-robin preemptive scheduler
  - 100ms time slices (10 ticks at 100Hz PIT)
  - Ready queue (doubly-linked list)
  - Thread states: READY, RUNNING, BLOCKED, SLEEPING, TERMINATED
- [2026-01-27] Thread management (`kernel/sched/thread.c`)
  - Thread Control Block (TCB) structure
  - thread_create() with 16KB stacks
  - thread_yield(), thread_sleep_ms(), thread_block/unblock()
  - thread_exit() with proper cleanup
  - Idle thread for CPU halt when no work
- [2026-01-27] Context switching (`kernel/sched/context.asm`)
  - Callee-saved register preservation (rbp, rbx, r12-r15, rflags)
  - Thread entry trampoline for new threads
- [2026-01-27] Scheduler (`kernel/sched/sched.c`)
  - sched_init(), sched_start(), sched_tick()
  - sched_reschedule() for voluntary/preemptive switching
  - sched_ready(), sched_remove() for queue management
  - Wake sleeping threads on timer tick
- [2026-01-27] IRQ-safe spinlocks (`kernel/sync/spinlock.h`)
  - spin_lock_irqsave() / spin_unlock_irqrestore()
  - Interrupt flag preservation across locks
- [2026-01-27] Critical fix: EOI sent before handler in IRQ handler
  - Allows context switching from interrupt context

### GDB Remote Debugger [VERIFIED]
- [2026-01-27] GDB stub (`kernel/debug/gdb_stub.c`)
  - Serial communication over COM2 (0x2F8)
  - GDB Remote Serial Protocol (RSP) implementation
  - Packet checksums, ACK/NAK handling
  - Register read/write (g/G packets)
  - Memory read/write (m/M packets)
  - Single-stepping via Trap Flag (TF)
  - Software breakpoints (INT3 opcode replacement)
  - INT1 (debug) and INT3 (breakpoint) exception handlers
  - Continue (c) and step (s) commands
- [2026-01-27] Integration with IDT (vectors 1 and 3)
- [2026-01-27] Make targets: run-gdb, run-gdb-wait

### Virtual File System [VERIFIED]
- [2026-01-27] VFS abstraction layer (`kernel/fs/vfs.c`, `kernel/fs/vfs.h`)
  - File types (file, dir, chardev, blkdev, pipe, symlink, socket)
  - Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND)
  - Error codes (VFS_ENOENT, VFS_ENOMEM, VFS_EEXIST, etc.)
  - Filesystem registration and mount API
  - File descriptor table (256 max)
  - Path resolution with mount traversal
  - File operations: open, close, read, write, seek, stat, truncate
  - Directory operations: mkdir, rmdir, unlink, readdir
  - Node reference counting
- [2026-01-27] Ramfs implementation (`kernel/fs/ramfs.c`, `kernel/fs/ramfs.h`)
  - Block-based file storage (4KB blocks)
  - Directory children linked list
  - File/directory operations (create, read, write, unlink, rmdir)
  - Slab cache for ramfs data structures
- [2026-01-27] String utilities (`kernel/lib/string.c`, `kernel/lib/string.h`)
  - strlen, strcmp, strncmp, strcpy, strncpy, strdup
  - strchr, strrchr
  - memcpy, memmove, memset, memcmp
- [2026-01-27] Bootloader multi-pass loading
  - Fixed kernel load to read in 64-sector chunks
  - Supports kernels > 64KB

### Process Management [VERIFIED]
- [2026-01-27] Process structure and process table (`kernel/process/process.c`)
  - process_t structure with PID, state, CR3, kernel stack, parent PID
  - Process states: UNUSED, EMBRYO, READY, RUNNING, BLOCKED, ZOMBIE, DEAD
  - Process table (256 max processes)
  - PID allocation with wraparound
  - process_create(), process_destroy()
  - process_get_by_pid(), process_current()
  - process_exit() with exit code
  - Kernel process (PID 0) created at init
  - 10 unit tests (all passing)
- [2026-01-27] Integration with threading subsystem
  - Kernel process owns boot thread
  - Per-process kernel stack allocation (16KB)

### User-Space Support [IMPLEMENTED]
- [2026-01-27] Address space management (`kernel/mm/as.c`, `kernel/mm/as.h`)
  - as_create() - Create new address space with kernel higher-half
  - as_clone() - Clone address space for fork (copies all user pages)
  - as_destroy() - Free all user-space pages and page tables
  - as_switch() - Switch CR3 to new address space
  - as_map_page(), as_alloc_page(), as_free_page()
  - User space: 0x0 - 0x7FFFFFFFFFFF, Kernel: 0xFFFF800000000000+
- [2026-01-27] Syscall infrastructure (`kernel/syscall/syscall.c`, `syscall_entry.asm`)
  - SYSCALL/SYSRET entry point with proper MSR setup (STAR, LSTAR, SFMASK, EFER.SCE)
  - syscall_frame_t for saving all registers
  - syscall_dispatch() - C dispatcher with handler table
  - Syscalls: exit (60), write (1), getpid (39), getppid (110), fork (57)
  - x86_64 calling convention: RAX=num, RDI/RSI/RDX/R10/R8/R9=args
- [2026-01-27] Ring 3 entry (`kernel/user/user_entry.c`, `ring3_enter.asm`)
  - ring3_enter() - Assembly IRETQ to switch to user mode
  - user_enter() - Sets TSS.RSP0, calls ring3_enter
  - Test user program embedded in kernel
  - GDT selectors: User CS=0x23, User SS=0x1B
- [2026-01-27] ELF loader (`kernel/loader/elf_loader.c`, `elf.h`)
  - elf_load() - Load ELF64 into address space
  - Validates ELF header (magic, class, machine)
  - Loads PT_LOAD segments with proper permissions (R/W/X)
  - Handles BSS (zeroes pages beyond p_filesz)
  - Returns entry point, base/end addresses
- [2026-01-27] fork() system call (`kernel/syscall/syscall.c`)
  - sys_fork_impl() - Creates child process with cloned address space
  - fork_child_entry() - Thread entry for child, returns via SYSRET with RAX=0
  - fork_child_return() - Assembly helper to restore frame and SYSRET
  - Parent returns child PID, child returns 0
- [2026-01-27] exec() system call (`kernel/syscall/syscall.c`)
  - sys_execve_impl() - Replace process image with new executable
  - Opens ELF file via VFS, reads into kernel buffer
  - Creates new address space and loads ELF via elf_load()
  - Sets up user stack at 0x7FFFFFF00000 (64KB)
  - Destroys old address space, updates process CR3
  - Modifies syscall frame to return to new entry point
  - Closes FD_CLOEXEC descriptors on exec
  - Does not return on success (returns to new program)
- [2026-01-27] Per-process file descriptor table (`kernel/fs/fd_table.c`)
  - fd_table_t structure with 256 entries per process
  - fd_table_create() - Create empty fd table
  - fd_table_destroy() - Close all files and free table
  - fd_table_clone() - Duplicate for fork (increment file ref counts)
  - fd_table_close_cloexec() - Close FD_CLOEXEC descriptors on exec
  - fd_table_alloc/free/get - Manage individual descriptors
  - FD_CLOEXEC flag support for close-on-exec behavior
  - Integrated with VFS (vfs.c uses per-process tables)
  - Integrated with process management (process.c creates/destroys tables)
  - fork() clones parent's fd table to child
  - Fallback to global table before process subsystem initialized

### HPET Timer [VERIFIED]
- [2026-01-31] HPET driver (`kernel/drivers/hpet.c`, `kernel/drivers/hpet.h`)
  - Initialize from ACPI-provided address (0xFED00000)
  - Read capabilities (timers, counter size, period)
  - Enable main counter, disable timer interrupts
  - hpet_read_counter() - Read 64-bit counter
  - hpet_get_ns/us/ms() - Get elapsed time
  - hpet_delay_ns/us/ms() - Precision busy-wait
  - hpet_get_frequency() - Returns counter frequency
- [2026-01-31] Verified: 100 MHz, 3 timers, 64-bit counter
- [2026-01-31] Verified: 10ms and 1000us delays accurate

### APIC Setup [VERIFIED]
- [2026-01-31] APIC subsystem (`kernel/apic/apic.c`, `kernel/apic/apic.h`)
  - Local APIC initialization (SVR, LVT masking, TPR, error clearing)
  - I/O APIC initialization (redirection table setup)
  - IRQ routing with ACPI override support
  - Legacy 8259 PIC disabled
  - lapic_eoi() for interrupt acknowledgment
  - IDT updated to use APIC EOI when enabled
  - Virtual address mapping at 0xFFFFFFFF90100000+ (outside 2MB pages)
- [2026-01-31] Verified: All 8 APIC tests pass
- [2026-01-31] Verified: Timer/keyboard interrupts work via APIC

### File I/O Syscalls [IMPLEMENTED]
- [2026-01-31] Enhanced file I/O syscalls (`kernel/syscall/syscall.c`)
  - sys_read() - Extended to read from VFS files (fd > 2)
  - sys_write() - Extended to write to VFS files (fd > 2)
  - sys_lseek() - Seek to position in file (SEEK_SET, SEEK_CUR, SEEK_END)
  - sys_fstat() - Get file status (size, type, permissions)
  - sys_dup() - Duplicate file descriptor
  - sys_dup2() - Duplicate to specific file descriptor
- [2026-01-31] VFS extensions (`kernel/fs/vfs.c`, `kernel/fs/vfs.h`)
  - vfs_dup() - Duplicate file descriptor with ref count management
  - vfs_dup2() - Duplicate to specific fd, closes target if open
- [2026-01-31] Syscall numbers (`kernel/syscall/syscall.h`)
  - SYS_FSTAT (5), SYS_LSEEK (8), SYS_DUP (32), SYS_DUP2 (33)
- [2026-01-31] User-kernel data transfer via copy_to_user/copy_from_user
- [2026-01-31] Chunked reads/writes for large buffers (256-byte chunks)

### Initial Ramdisk (Initrd) [VERIFIED]
- [2026-01-31] Initrd subsystem (`kernel/initrd/initrd.c`, `kernel/initrd/initrd.h`)
  - CPIO newc format parser (magic "070701")
  - Bootloader loads initrd from disk to 0x20000 temp buffer
  - BootInfo passes initrd_start and initrd_size to kernel
  - initrd_init() validates CPIO magic, counts entries
  - initrd_mount() creates files/directories in ramfs
  - initrd_find() searches for file by path
  - PMM reserves initrd pages to prevent allocation
- [2026-01-31] Bootloader support (`boot/bios/stage2.asm`)
  - INITRD_SIZE build-time configuration
  - Calculates initrd LBA from kernel size
  - Reads initrd to temp buffer using INT 13h
  - BOOT_FLAG_INITRD indicates initrd presence
- [2026-01-31] Makefile support
  - `make INITRD=path/to/initrd.cpio image-bios-initrd`
  - Automatically sets KERNEL_SIZE and INITRD_SIZE for bootloader
  - Writes initrd after kernel in disk image
- [2026-01-31] Verified: 6 files from initrd mounted to ramfs (/bin/init, /etc/config, /etc/hostname)

### ACPI Table Parsing [VERIFIED]
- [2026-01-31] ACPI subsystem (`kernel/acpi/acpi.c`, `kernel/acpi/acpi.h`)
  - RSDP discovery (boot-provided hint, EBDA, BIOS ROM search)
  - RSDT/XSDT parsing with checksum validation
  - MADT parsing (Local APIC address, CPUs, I/O APICs, interrupt overrides)
  - FADT parsing (PM timer port, SCI interrupt)
  - HPET parsing (base address)
  - Parsed acpi_info_t structure for kernel use
  - acpi_isa_irq_to_gsi() - IRQ to GSI mapping with overrides
  - acpi_get_irq_flags() - Get polarity/trigger mode for IRQs
  - Comprehensive test suite (10 tests in DEBUG_TESTS mode)
- [2026-01-31] Verified: Local APIC at 0xFEE00000, I/O APIC at 0xFEC00000
- [2026-01-31] Verified: HPET at 0xFED00000, IRQ overrides (timer on GSI 2)
- [2026-01-31] Ready for APIC initialization

### Procfs Filesystem [VERIFIED]
- [2026-01-31] Procfs implementation (`kernel/fs/procfs.c`, `kernel/fs/procfs.h`)
  - Virtual filesystem for process and system information
  - Dynamic file content generation on read
  - Mounted at /proc
  - Files: /proc/version, /proc/meminfo, /proc/uptime, /proc/cpuinfo, /proc/stat
  - Extensible node registration system
  - Integration with VFS (registered as "procfs" filesystem type)
- [2026-01-31] Verified: /proc/meminfo reads correctly with memory stats

### Extended Syscalls [VERIFIED]
- [2026-01-31] Added 30+ new syscalls following Linux x86_64 ABI (`kernel/syscall/syscall.c`)
  - **Process/Identity**: getpid, getppid, getuid, geteuid, getgid, getegid, setuid, setgid
  - **Session/Group**: setsid, getpgid, setpgid, getsid
  - **System Info**: uname (returns "Kurios2")
  - **Time**: gettimeofday, clock_gettime, clock_getres, nanosleep, sched_yield
  - **Memory**: brk (heap management), mmap (anonymous only), munmap, mprotect
  - **Filesystem**: stat, access, getcwd, chdir, mkdir, rmdir, unlink, truncate, ftruncate, getdents, readlink
  - **Signals**: sigaction, sigprocmask, sigreturn (stubs - signal delivery not implemented)
  - **I/O**: ioctl (stub), pipe (stub)
  - **Other**: syslog (stub)
- [2026-01-31] Updated syscall.h with new syscall numbers and structures
  - timespec_t, timeval_t, timezone_t, utsname_t, linux_dirent64_t
  - mmap flags (PROT_*, MAP_*)
  - access() modes (F_OK, R_OK, W_OK, X_OK)
  - Clock IDs (CLOCK_REALTIME, CLOCK_MONOTONIC, etc.)
- [2026-01-31] Added process fields for syscall support (`kernel/process/process.h`)
  - pgrp (process group ID)
  - session_id (session ID)
  - brk (program break for heap)
  - cwd[256] (current working directory)
- [2026-01-31] Kernel testing mode for access_ok (`kernel/mm/uaccess.c`)
  - uaccess_set_kernel_testing() - Allow kernel addresses in tests
  - Enables testing syscalls from kernel code
- [2026-01-31] Comprehensive test suite (all tests passing)
  - 26+ syscall tests in syscall_run_tests()
  - Tests: identity, time, memory, filesystem, I/O, scheduling, signals, error handling

### Higher-Half Kernel [VERIFIED]
- [2026-01-24] Updated linker script
  - Kernel virtual base: 0xFFFFFFFF80000000
  - Kernel physical base: 0x100000 (1MB)
  - Proper LMA/VMA separation
  - Exported symbols for kernel use
- [2026-01-24] Updated bootloader paging
  - Identity map first 4GB
  - Higher-half mapping for kernel (16MB)
  - PML4[511] -> PDPT[510] -> PD for kernel
- [2026-01-24] Kernel entry point
  - BSS zeroing
  - Stack setup in higher half
  - boot_info passing
- [2026-01-24] Common types header (`include/types.h`)
  - Page size constants
  - Alignment macros
  - Bit manipulation
  - Compiler attributes
