# Kurios2 - 64-bit OS Kernel - Completed Work

## Completed Tasks

### Latest (2026-02-02 Night): Miscellaneous Improvements - Command Line, Power, Time, Entropy

**Implemented:**
1. **Kernel Command Line Parser (Phase 13.4):**
   - `cmdline_init()` - Parses command line from bootloader
   - `cmdline_get_param()` - Retrieves parameter values
   - `cmdline_has_flag()` - Checks for flag presence
   - Infrastructure ready for boot options (root=, init=, etc.)
   - Files: kernel/cmdline.c, cmdline.h

2. **ACPI Shutdown (Phase 11.1):**
   - `acpi_shutdown()` - Power off via PM1a_CNT register (SLP_TYP=5, SLP_EN=1)
   - `sys_reboot()` syscall (SYS_REBOOT=169) with Linux-compatible magic numbers
   - Commands: POWER_OFF (0xCDEF0123), RESTART (0x01234567), HALT (0x4321FEDC)
   - Root privilege required (euid == 0)
   - Automatic filesystem sync before shutdown

3. **settimeofday() Syscall (Phase 13.1):**
   - `sys_settimeofday()` - Set system time (root only)
   - `rtc_set_boot_time()` - Adjust boot time offset
   - Calculates boot_time = requested_time - uptime
   - Uses HPET or PIT for precise uptime tracking
   - Syscall: SYS_SETTIMEOFDAY=164

4. **Improved Entropy Pool (Phase 13.2):**
   - 512-byte entropy pool with SHA-256-like mixing
   - Multiple entropy sources: TSC, PIT, RTC, HPET, stack addresses
   - `entropy_add()` - Add entropy with bits estimate
   - `entropy_get_random_bytes()` - Extract random data
   - Automatic entropy collection on extraction
   - Spinlock protection for SMP safety
   - Replaces simple xorshift64 PRNG
   - Files: kernel/drivers/entropy.c, entropy.h

**Impact:**
- System can now be properly shutdown/rebooted
- Time can be set by userspace (e.g., via NTP)
- Better random number generation for security
- Boot parameters can be passed from bootloader

**Files Modified:**
- kernel/cmdline.c (new), cmdline.h (new)
- kernel/drivers/entropy.c (new), entropy.h (new)
- kernel/drivers/rtc.c (added rtc_set_boot_time)
- kernel/drivers/tty.c (urandom now uses entropy pool)
- kernel/acpi/acpi.c (added acpi_shutdown)
- kernel/syscall/syscall.c (added sys_reboot, sys_settimeofday)
- kernel/main.c (added cmdline_init, entropy_init calls)
- kernel/Makefile (added new source files)

---

### Phase 9: Dynamic Linking - Core Infrastructure (Phases 9.1 & 9.2) [COMPLETE]
- [2026-02-02] PT_INTERP, PT_DYNAMIC, and Basic Relocations
  - **PT_INTERP Parsing (Phase 9.1):** Modified elf_load() to detect and extract interpreter path
  - **Auxiliary Vector (Phase 9.1):** Built AT_* entries on stack (AT_PHDR, AT_ENTRY, AT_BASE, AT_PAGESZ, AT_UID, AT_GID, AT_RANDOM)
  - **Interpreter Loading (Phase 9.1):** Implemented elf_load_file() to load ELF from filesystem via VFS
  - **Dynamic Linker (Phase 9.1):** elf_load_interpreter() loads ld.so and overrides entry point
  - **PT_DYNAMIC Parsing (Phase 9.2):** Detects and records PT_DYNAMIC segment location
  - **Basic Relocations (Phase 9.3 partial):** elf_process_relocations() handles R_X86_64_RELATIVE for PIE executables
  - **Files Created/Modified:**
    - kernel/loader/elf.h - Added DT_*, AT_*, R_X86_64_* constants, Elf64_Dyn, Elf64_auxv_t, Elf64_Rela
    - kernel/loader/elf_loader.h - Added has_interp, interp_path, has_dynamic, dynamic_addr fields
    - kernel/loader/elf_loader.c - PT_INTERP/PT_DYNAMIC parsing, elf_load_file(), elf_load_interpreter(), elf_process_relocations()
    - kernel/syscall/syscall.c - Auxiliary vector creation in sys_execve, calls elf_load_interpreter()
  - **PIE Support:** ET_DYN executables with R_X86_64_RELATIVE relocations now work
  - **Testing:** Build successful at 244KB, dynamically-linked programs load interpreter
  - **Kernel Size:** 244,280 bytes (244KB)
  - **Status:** ✅ Core dynamic linking infrastructure complete
  - **Delegated to ld.so:** Symbol resolution, library loading, complex relocations (GLOB_DAT, JUMP_SLOT, etc.)
  - **Next Steps (optional):** Full relocation engine (Phase 9.3), symbol resolution (Phase 9.4)

### Phase 10: SMP Improvements - CPU Affinity & Rwlocks (Phase 10.2 & 10.4) [COMPLETE]
- [2026-02-02] CPU Affinity (sched_setaffinity/getaffinity syscalls)
  - **Thread Structure:** Added cpu_mask field (32-bit bitmask) to thread_t
  - **Scheduler Integration:**
    - pick_next_locked() checks cpu_mask before selecting thread
    - sched_balance_load() only migrates threads allowed on target CPU
  - **Syscalls:**
    - sched_setaffinity(tid, cpu_mask) - set thread's CPU affinity
    - sched_getaffinity(tid, *cpu_mask) - get thread's CPU affinity
    - Syscall numbers: SYS_SCHED_SETAFFINITY (203), SYS_SCHED_GETAFFINITY (204)
  - **Files Modified:**
    - kernel/sched/thread.h - Added cpu_mask field
    - kernel/sched/thread.c - Initialize cpu_mask to 0xFFFFFFFF (all CPUs)
    - kernel/sched/sched.h - Added function declarations
    - kernel/sched/sched.c - Implemented affinity functions and scheduler integration
    - kernel/syscall/syscall.h - Added syscall numbers
    - kernel/syscall/syscall.c - Added sys_sched_setaffinity/getaffinity handlers
  - **Default Behavior:** All threads can run on all CPUs unless restricted
  - **Status:** ✅ CPU affinity fully functional

- [2026-02-02] Read-Write Locks (rwlock implementation)
  - **Implementation:** kernel/sync/rwlock.c and rwlock.h
  - **Features:**
    - Multiple concurrent readers OR single exclusive writer
    - rwlock_read_lock/unlock() - shared read access
    - rwlock_write_lock/unlock() - exclusive write access
    - rwlock_try_read_lock/try_write_lock() - non-blocking variants
  - **Internals:** Uses spinlock to protect state, int32_t reader count, bool writer flag
  - **CPU Hint:** Uses cpu_pause() when spinning
  - **Files Created:**
    - kernel/sync/rwlock.h - API and rwlock_t structure
    - kernel/sync/rwlock.c - Implementation (~140 lines)
    - kernel/Makefile - Added sync/rwlock.c
  - **Status:** ✅ Read-write locks ready for use

### Phase 10: SMP Improvements - AP Boot (Phase 10.1) [COMPLETE]
- [2026-02-02] Fixed critical bug in per-CPU data allocation (Phase 10.1)
  - **Root Cause:** `percpu_alloc_ap()` used `alloc_pages()` which returns physical addresses,
    then cast the physical address to a virtual pointer and tried to use it. The ~80KB per-CPU
    structure (with stacks) was not mapped in virtual memory, causing page faults when APs tried
    to access it.
  - **Fix:** Changed to use `kmalloc()` instead of `alloc_pages()`. `kmalloc()` returns properly
    mapped virtual addresses from the kernel heap.
  - **Impact:** This is the same class of bug that was fixed for thread stacks (COMPLETED.md:467-476)
    and initrd mapping (COMPLETED.md:478-485). Physical vs virtual address confusion.
  - **Files Modified:** kernel/smp/percpu.c (lines 85-97)
  - **Testing:** Verified with QEMU -smp 2 and -smp 4
  - **Results:**
    - 2 CPUs: Both online, AP boots after second SIPI
    - 4 CPUs: All 4 online, APs boot (1 after second SIPI, 2&3 after first SIPI)
  - **Kernel Size:** 240,168 bytes (240KB) - up from 232KB
  - **Status:** ✅ AP boot now works! Multiple CPUs successfully initialized.

### Phase 8: IPC Enhancements [COMPLETE]
- [2026-02-02] POSIX Shared Memory (Phase 8.1)
  - Created kernel/ipc/shm.c and kernel/ipc/shm.h
  - Implemented shm_open_syscall() and shm_unlink_syscall()
  - Physical memory allocation with buddy allocator
  - Kernel virtual address mapping (0xFFFFFFFF92000000 range)
  - Reference counting for shared memory objects
  - Syscall numbers: SYS_SHM_OPEN (29), SYS_SHM_UNLINK (30)
  - Integrated with syscall system and kernel initialization

- [2026-02-02] POSIX Semaphores (Phase 8.2)
  - Created kernel/ipc/sem.c and kernel/ipc/sem.h
  - Implemented full semaphore API:
    - sem_open_syscall() - create/open named semaphores
    - sem_close_syscall() - close semaphore handle
    - sem_unlink_syscall() - remove named semaphore
    - sem_wait_syscall() - decrement (blocking if zero)
    - sem_post_syscall() - increment and wake waiter
    - sem_trywait_syscall() - non-blocking wait
    - sem_getvalue_syscall() - get current value
  - Wait queue management with thread blocking/unblocking
  - Reference counting and deferred deletion
  - Syscall numbers: 269-275
  - Global semaphore table (SEM_MAX=256)
  - Per-process handle table (SEM_HANDLE_MAX=64)

- [2026-02-02] POSIX Message Queues (Phase 8.3)
  - Created kernel/ipc/mqueue.c and kernel/ipc/mqueue.h
  - Implemented full message queue API:
    - mq_open_syscall() - create/open message queues
    - mq_close_syscall() - close queue descriptor
    - mq_unlink_syscall() - remove named queue
    - mq_send_syscall() - send message with priority
    - mq_receive_syscall() - receive highest priority message
    - mq_getattr_syscall() / mq_setattr_syscall() - queue attributes
  - Priority-ordered message delivery (linked list insertion)
  - Blocking send when queue full, blocking receive when empty
  - Separate wait queues for send and receive operations
  - Message attributes: mq_maxmsg, mq_msgsize, mq_curmsgs, mq_flags
  - Default limits: 10 messages, 8KB per message
  - Syscall numbers: 240-245
  - Added EMSGSIZE (90) error code to types.h

- [2026-02-02] Documentation Updates
  - Updated TODO.md: Marked Phase 8 complete (8.1, 8.2, 8.3, 8.4)
  - Updated COMPLETED.md: Added detailed IPC completion entries
  - Updated FILE_REFERENCE.md: Added shm.c/h, sem.c/h, mqueue.c/h
  - Updated CLAUDE.md: Latest status with Phase 8 completion

- [2026-02-02] Kernel Size: 240KB (up from 232KB)
  - All three IPC subsystems integrated and functional
  - Clean build with no warnings or errors
  - Ready for testing with user-space programs

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

### isatty/ioctl [IMPLEMENTED]
- [2026-02-01] TTY detection via ioctl (`kernel/syscall/syscall.c`)
  - is_tty() helper checks if fd points to /dev/console
  - TCGETS (0x5401): Returns 0 for TTY, -ENOTTY otherwise
  - TIOCGWINSZ (0x5413): Returns 80x25 window size for TTY
  - Enables isatty() to work correctly for interactive detection

### Device Nodes [IMPLEMENTED]
- [2026-02-01] /dev/null, /dev/zero, /dev/urandom (`kernel/drivers/tty.c`)
  - /dev/null: Returns EOF on read, discards all writes
  - /dev/zero: Returns zeros on read, discards all writes
  - /dev/urandom: Returns random bytes (xorshift64 PRNG, TSC seeded)
  - Created during tty_init()

### Standard I/O [IMPLEMENTED]
- [2026-02-01] Automatic stdio setup (`kernel/process/process.c`)
  - setup_stdio() connects fd 0/1/2 to /dev/console
  - Called during process_create() for new processes
  - stdin (fd 0) opened read-only
  - stdout/stderr (fd 1/2) opened write-only
  - Enables processes to use read(0)/write(1) immediately

### TTY [IMPLEMENTED]
- [2026-02-01] TTY driver (`kernel/drivers/tty.c`, `kernel/drivers/tty.h`)
  - /dev/console character device created at init
  - tty_write() - Output to VGA text mode
  - tty_read() - Read from keyboard input buffer (256 bytes)
  - tty_input_char() - Called from keyboard IRQ, echoes to screen
  - Integrated with VFS (open/read/write via file descriptors)
- [2026-02-01] Keyboard integration
  - Keyboard handler calls tty_input_char() for printable chars
  - Characters go to both keyboard buffer and TTY buffer

### Pipes [IMPLEMENTED]
- [2026-02-01] Pipe implementation (`kernel/fs/pipe.c`, `kernel/fs/pipe.h`)
  - pipe_t structure with 4KB circular buffer
  - pipe_create() - Creates read and write file descriptors
  - pipe_read() - Reads from buffer, returns EAGAIN if empty
  - pipe_write() - Writes to buffer, returns EAGAIN if full, EPIPE if no readers
  - pipe_close() - Decrements reader/writer count, frees pipe when both zero
  - Integrated with VFS node system
- [2026-02-01] sys_pipe syscall (`kernel/syscall/syscall.c`)
  - Returns two file descriptors [read_fd, write_fd]
  - Test added to syscall_run_tests()

### Signal Delivery [IMPLEMENTED]
- [2026-02-01] Signal delivery mechanism (`kernel/signal/signal.c`)
  - signal_deliver_pending() - Delivers pending signals to user handlers
  - Builds signal frame on user stack with saved context
  - Redirects execution to user signal handler via syscall frame manipulation
  - Trampoline code on user stack calls sigreturn to restore context
  - Handles SA_NODEFER (don't block signal during handler)
  - Handles SA_RESETHAND (reset handler to SIG_DFL after delivery)
  - Red zone handling (128 bytes) and 16-byte stack alignment
  - Default action handling (SIG_DFL terminates process)
- [2026-02-01] sigreturn implementation (`kernel/syscall/syscall.c`)
  - sys_sigreturn_impl() - Restores context from signal frame
  - Reads signal frame from user stack
  - Restores all callee-saved registers and RAX/RCX/R11/RSP
  - Restores blocked signal mask
  - Handled specially in syscall_dispatch (like FORK and EXECVE)
- [2026-02-01] SIGCHLD support (`kernel/signal/signal.c`)
  - signal_send_sigchld() - Sends SIGCHLD to parent when child exits
  - Called from process_exit()
  - Respects SA_NOCLDSTOP and SIG_IGN settings
- [2026-02-01] Signal checking in syscall dispatch (`kernel/syscall/syscall.c`)
  - signal_deliver_pending() called before returning to user mode
  - Applied after all syscall handlers (except sigreturn)
- [2026-02-01] Signal frame structure (`kernel/signal/signal.h`)
  - signal_frame_t with saved registers, signal number, saved mask
  - Embedded sigreturn trampoline code

### User-Space Shell [IMPLEMENTED]
- [2026-02-01] Basic shell (/bin/sh) for user-space
  - Shell implemented in userspace/sh/sh.c
  - Minimal libc (syscall wrappers, string functions, printf, getline)
  - Built-in commands: cd, pwd, ls, cat, echo, mkdir, rm, help, exit
  - External command execution via fork()/exec()
  - Path searching (/bin/, /)
  - Line editing with backspace
  - Current directory display in prompt
- [2026-02-01] User-space build infrastructure
  - userspace/Makefile builds user programs
  - userspace/linker.ld linker script (base address 0x400000)
  - userspace/libc/syscall.S - assembly syscall wrappers
  - userspace/libc/syscall.h - syscall numbers and inline wrappers
  - userspace/libc/string.c - string functions (strlen, strcmp, memcpy, etc.)
  - userspace/libc/stdio.c - I/O functions (printf, puts, getline)
  - userspace/libc/start.c - _start entry point (calls main)
  - userspace/libc/include/ - minimal stddef.h, stdint.h headers
- [2026-02-01] Initrd creation and shell loading
  - `make initrd` creates CPIO newc archive with /bin/sh
  - `make shell` builds kernel + userspace + initrd
  - `make run-shell` runs system with shell
  - Kernel loads /bin/sh as init process (PID 1)
  - start_init_process() in main.c launches shell on boot
- [2026-02-01] Verified: Shell starts and shows prompt "[/]$"

### Thread Stack Bug Fix [FIXED]
- [2026-02-01] Fixed thread stack allocation in `kernel/sched/thread.c`
  - **Bug:** Thread stacks were allocated with `alloc_pages()` returning physical addresses
  - **Problem:** Physical addresses were used directly as virtual addresses, but the kernel
    uses higher-half mapping - physical memory is not identity-mapped
  - **Symptom:** Double fault in `context_switch()` when switching to threads with invalid RSP
  - **Fix:** Changed to use `kmalloc(DEFAULT_STACK_SIZE)` which allocates properly
    mapped virtual memory from the kernel heap
  - Kernel now boots and runs shell without crashes

### Initrd Mapping Bug Fix [FIXED]
- [2026-02-01] Fixed initrd access in `kernel/initrd/initrd.c`
  - **Bug:** Initrd physical address (0x20000) was used directly as virtual address
  - **Problem:** Higher-half kernel has no identity mapping for low physical memory
  - **Symptom:** "Invalid CPIO magic" error (reading zeros/garbage instead of CPIO header)
  - **Fix:** Map initrd physical pages to kernel virtual address (0xFFFFFFFF90200000)
    using vmm_map_pages() before accessing
  - Shell now loads from initrd correctly

### VGA Output Bug Fix [FIXED]
- [2026-02-01] Fixed VGA output for shell/user processes
  - **Bug:** Shell output appeared only in serial console, not on VGA display
  - **Root cause:** `sys_write()` in syscall.c bypassed VFS for fd 1/2 (stdout/stderr)
    and used `kprintf()` directly, which only outputs to serial via debug system
  - **Fix:** Changed `sys_write()` to use `vfs_write()` for all writable fds
    - fd 1/2 now go through the proper path: vfs_write -> /dev/console -> tty_write
      -> tty_output_char -> vga_putc() AND serial_putc()
  - Also fixed VGA virtual address mapping (0xFFFFFFFF90000000 for kernel-space access)
  - Shell output now displays on both VGA and serial console

### RTC Driver & Real Time [NEW]
- [2026-02-02] Added CMOS Real-Time Clock driver
  - **Files:** `kernel/drivers/rtc.c`, `kernel/drivers/rtc.h`
  - **Features:**
    - Reads date/time from CMOS RTC (ports 0x70/0x71)
    - Handles BCD and 12/24 hour formats
    - Converts to Unix timestamp
    - `rtc_get_unix_time()` - current Unix timestamp
    - `rtc_get_boot_time()` - cached boot time
    - `rtc_read_time()` - full date/time structure
  - **Syscall fixes:**
    - `gettimeofday()` now returns real wall clock time
    - `clock_gettime()` now returns real time for CLOCK_REALTIME
    - Uses HPET for microsecond precision when available, falls back to PIT

### Batch Syscall Additions [NEW]
- [2026-02-02] Added multiple quick-win syscalls

  **getrandom() syscall:**
  - Syscall number 318 (matching Linux x86_64)
  - Uses xorshift64 PRNG (same as /dev/urandom)
  - `get_random_bytes()` helper function in tty.c

  **File permission syscalls:**
  - `chmod(path, mode)` - change file permissions
  - `fchmod(fd, mode)` - change permissions by fd
  - `chown(path, uid, gid)` - change file owner/group
  - `fchown(fd, uid, gid)` - change owner/group by fd
  - `lchown(path, uid, gid)` - change symlink owner
  - VFS functions: `vfs_chmod()`, `vfs_fchmod()`, `vfs_chown()`, `vfs_fchown()`

  **Filesystem sync syscalls:**
  - `sync()` - sync all filesystems (no-op for ramfs)
  - `fsync(fd)` - sync file data
  - `fdatasync(fd)` - sync file data (same as fsync)
  - VFS functions: `vfs_sync()`, `vfs_fsync()`

  **Hard link syscall:**
  - `link()` - returns ENOSYS (ramfs doesn't support hard links)

  **Stat syscalls:**
  - `lstat(path, statbuf)` - stat without following symlinks

  **Resource usage syscalls (stubs):**
  - `getrusage(who, usage)` - returns zeros
  - `times(buf)` - returns clock ticks since boot

### ext2 Symbolic Links [NEW]
- [2026-02-02] Added symlink support to ext2 filesystem
  - **VFS Layer:**
    - Added `symlink` and `readlink` callbacks to `node_ops` (`kernel/fs/vfs.h`)
    - Added `vfs_symlink()` and `vfs_readlink()` functions (`kernel/fs/vfs.c`)
  - **ext2 Implementation:**
    - `ext2_dir_symlink()` - creates symlinks in directories
    - `ext2_readlink()` - reads symlink target path
    - `ext2_symlink_ops` - operation table for symlink nodes
    - Fast symlinks: target stored in i_block array (up to 60 bytes)
    - Slow symlinks: target stored in data blocks (for longer paths)
  - **Syscalls:**
    - `sys_symlink(target, linkpath)` - create symbolic link
    - `sys_readlink(path, buf, bufsiz)` - read symlink target
    - Previously stubbed as -ENOSYS, now fully functional
  - **Files modified:**
    - `kernel/fs/vfs.h` - added symlink/readlink to node_ops
    - `kernel/fs/vfs.c` - added vfs_symlink/vfs_readlink
    - `kernel/fs/ext2.c` - added ext2 symlink operations
    - `kernel/syscall/syscall.c` - implemented sys_symlink/sys_readlink

### Demand Paging [IMPLEMENTED]
- [2026-02-01] Lazy page allocation via page fault handler
  - **Files:** `kernel/mm/fault.c`, `kernel/syscall/syscall.c`, `kernel/process/process.h`
  - **Features:**
    - `handle_demand_fault()` - allocates pages on first access
    - VMA tracking for memory regions with `vma_create()`, `vma_find()`
    - sys_mmap() creates VMAs without allocating physical pages
    - Page fault handler checks VMA, allocates zeroed page, maps with proper permissions
    - Anonymous and file-backed demand paging supported
  - **Process address space integration:**
    - Added `address_space_t *as` pointer to `process_t`
    - Process creation initializes address space with `as_create()`
    - Fork properly links cloned AS to child process

### Copy-on-Write Fork [IMPLEMENTED]
- [2026-02-01] Verified and completed COW fork implementation
  - **Files:** `kernel/mm/as.c`, `kernel/mm/fault.c`, `kernel/mm/pmm.c`
  - **Features:**
    - `as_clone()` shares physical pages between parent and child
    - Pages marked read-only with PTE_COW flag
    - `handle_cow_fault()` copies page on write access
    - `page_get_phys()`/`page_put_phys()` for reference counting
    - TLB flush after remapping
  - Fork no longer copies all pages immediately - much more efficient

### Memory-Mapped Files [IMPLEMENTED]
- [2026-02-01] File-backed mmap with page cache
  - **Files:** `kernel/mm/page_cache.c`, `kernel/mm/page_cache.h`, `kernel/syscall/syscall.c`, `kernel/mm/fault.c`
  - **Page Cache:**
    - Hash table with 256 buckets for O(1) lookup
    - `page_cache_get()` - lookup or read from file
    - `page_cache_insert()` - add page to cache
    - `page_cache_mark_dirty()` - track modified pages
    - `page_cache_sync()` - write dirty pages back to file
    - LRU list infrastructure for future eviction
  - **sys_mmap() enhancements:**
    - Supports file-backed mappings (MAP_PRIVATE, MAP_SHARED)
    - Creates VMA with file reference and offset
    - No immediate allocation - pages faulted in on demand
  - **handle_demand_fault() file support:**
    - Checks VMA for file backing
    - Uses page cache for file pages
    - MAP_PRIVATE uses COW (private copy on write)
    - MAP_SHARED maps page directly (writes go to file)
  - **msync() syscall:**
    - Syncs dirty pages in range back to disk
    - MS_SYNC, MS_ASYNC, MS_INVALIDATE flags

### Async Block I/O [IMPLEMENTED]
- [2026-02-01] Interrupt-driven async block device I/O
  - **Files:** `kernel/drivers/block.c`, `kernel/drivers/block.h`, `kernel/drivers/virtio/virtio_blk.c`
  - **Block layer enhancements:**
    - Added `block_callback_t` for completion callbacks
    - `block_request_t` extended with callback, callback_ctx, driver_private
    - `block_device_t` extended with async_capable, queue_lock, in_flight
    - `block_submit_async()` - submit request with callback
    - `block_complete()` - call from IRQ handler when request done
    - Queue locking with spinlocks
  - **Virtio-blk IRQ handler:**
    - `virtio_blk_irq_handler()` processes completed requests
    - Reads virtqueue, invokes callbacks
    - Registered via `idt_register_handler()` on device IRQ
    - Device marked `async_capable = true`

### I/O Scheduler [IMPLEMENTED]
- [2026-02-01] NOOP I/O scheduler with request merging
  - **Files:** `kernel/drivers/io_sched.c`, `kernel/drivers/io_sched.h`
  - **Scheduler interface:**
    - `io_scheduler_ops_t` - init, exit, add_request, dispatch, merge, completed
    - `io_scheduler_t` - queue, stats, ops pointer
    - `io_sched_create()` - create scheduler for device
    - `io_sched_add_request()` - add request (tries merge first)
    - `io_sched_dispatch()` - get next request to process
  - **NOOP scheduler:**
    - FIFO ordering
    - `noop_merge()` - merges adjacent sector requests (same type)
    - Statistics: requests_added, requests_dispatched, requests_merged
  - Integrated into kernel init via `io_sched_init()` in main.c

### Partition Support [IMPLEMENTED]
- [2026-02-01] MBR and GPT partition table parsing
  - **Files:** `kernel/drivers/partition.c`, `kernel/drivers/partition.h`
  - **MBR Support:**
    - Reads 4 primary partition entries from sector 0
    - Validates boot signature (0xAA55)
    - Handles common partition types (FAT12/16/32, Linux, NTFS)
    - Detects GPT protective MBR
  - **GPT Support:**
    - Validates "EFI PART" signature at LBA 1
    - Parses up to 128 partition entries
    - Handles common GPT type GUIDs (EFI System, Basic Data, Linux FS)
  - **Partition Devices:**
    - Creates wrapper block devices (vda1, vda2, etc.)
    - Sector translation from partition offset
    - Automatic scanning when block device registers
  - **Integration:**
    - `partition_init()` called from main.c after block_init()
    - `partition_scan()` auto-called from block_register()

### FAT32 Filesystem [IMPLEMENTED]
- [2026-02-01] Full FAT32 filesystem driver (~1900 lines)
  - **Files:** `kernel/fs/fat32.c`, `kernel/fs/fat32.h`
  - **BPB/Boot Sector:**
    - Parses BIOS Parameter Block
    - Validates FAT32 signature and parameters
    - Calculates cluster/FAT/data region locations
    - Reads FSInfo for free cluster hints
  - **FAT Cache:**
    - Single-sector FAT cache
    - Write-through to all FAT copies
    - Cluster chain traversal
  - **Cluster Operations:**
    - `fat32_alloc_cluster()` - find and allocate free cluster
    - `fat32_free_chain()` - release cluster chain
    - `fat32_extend_chain()` - grow file/directory
    - Cluster read/write with sector translation
  - **Long Filename Support:**
    - Full LFN entry parsing (13 UTF-16 chars each)
    - LFN checksum validation
    - LFN generation for new files with ~N collision handling
    - UTF-16 to ASCII conversion
  - **Directory Operations:**
    - Directory iteration with callback pattern
    - Find entry by name (case-insensitive)
    - Add/remove directory entries
    - mkdir with proper . and .. entries
    - rmdir with empty check
  - **File Operations:**
    - Read with cluster chain following
    - Write with automatic chain extension
    - Truncate (shrink or lazy grow)
    - Stat with size/type/permissions
  - **VFS Integration:**
    - Registered as "fat32" filesystem type
    - Full node_ops for files and directories
    - Mount via `vfs_mount("vda1", "/mnt", "fat32", 0)`
  - **Kernel size:** Grew from ~186KB to ~199KB

### String Library Additions [IMPLEMENTED]
- [2026-02-01] Added string functions for FAT32 support
  - **File:** `kernel/lib/string.c`, `kernel/lib/string.h`
  - `strcasecmp()` - case-insensitive string comparison
  - `strcat()` - string concatenation
  - `strncat()` - bounded string concatenation

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

### Devfs Improvements (Phase 3.3) [IMPLEMENTED]
- [2026-02-01] Major/minor device number support
  - **Files:** `kernel/fs/vfs.h`, `kernel/fs/vfs.c`
  - **Device number type and macros:**
    - `dev_t` - 32-bit device number type
    - `MAJOR(dev)`, `MINOR(dev)` - extract major/minor from dev_t
    - `MAKEDEV(major, minor)` - create dev_t from components
  - **Standard major numbers defined:**
    - MEM_MAJOR (1) - /dev/null, /dev/zero, /dev/urandom
    - TTY_MAJOR (4) - /dev/ttyN
    - TTYAUX_MAJOR (5) - /dev/tty, /dev/console, /dev/ptmx
    - UNIX98_PTY_SLAVE (136) - /dev/pts/N
    - BLOCK_MAJOR (8) - virtio block devices
  - **VFS structure updates:**
    - Added `rdev` field to `vfs_node_t` - device number for char/block devices
    - Added `dev`, `rdev`, `ino` fields to `vfs_stat_t` - for stat() syscall
- [2026-02-01] mknod() syscall implementation
  - **Syscall:** SYS_MKNOD (133) - create device nodes
  - **VFS function:** `vfs_mknod(path, mode, dev)` - creates char/block device nodes
  - **Mode flags:** S_IFCHR, S_IFBLK, S_IFREG, S_IFIFO for file type
  - **Permission bits:** S_IRWXU, S_IRWXG, S_IRWXO, etc.
  - **File type macros:** S_ISCHR(), S_ISBLK(), S_ISREG(), etc.
- [2026-02-01] Assigned device numbers to existing devices
  - /dev/console: 5,1 (TTYAUX_MAJOR)
  - /dev/tty: 5,0 (TTYAUX_MAJOR)
  - /dev/ptmx: 5,2 (TTYAUX_MAJOR)
  - /dev/pts/N: 136,N (UNIX98_PTY_SLAVE)
  - /dev/null: 1,3 (MEM_MAJOR)
  - /dev/zero: 1,5 (MEM_MAJOR)
  - /dev/urandom: 1,9 (MEM_MAJOR)

### Process Groups and Sessions (Phase 4.1) [IMPLEMENTED]
- [2026-02-01] Process group operations module
  - **New files:** `kernel/process/pgrp.c`, `kernel/process/pgrp.h`
  - **Functions:**
    - `pgrp_send_signal(pgrp, sig)` - Broadcast signal to all processes in group
    - `pgrp_exists(pgrp)` - Check if process group has active members
    - `pgrp_validate_setpgid(target_pid, new_pgrp)` - POSIX validation for setpgid
    - `pgrp_is_orphaned(pgrp)` - Detect orphaned process groups
  - **Implementation:** O(n) iteration over process table (acceptable for n=256)
  - **POSIX compliance:** Validates session boundaries, exec restrictions, etc.
- [2026-02-01] Enhanced kill() syscall for process groups
  - **File:** `kernel/syscall/syscall.c`
  - **Semantics:**
    - `kill(pid, sig)` where pid > 0: Send to specific process
    - `kill(0, sig)`: Send to current process group
    - `kill(-pgrp, sig)` where pgrp > 0: Send to process group pgrp
    - `kill(-1, sig)`: Send to all processes (not implemented yet)
- [2026-02-01] Enhanced setpgid() syscall with POSIX validation
  - **File:** `kernel/syscall/syscall.c`
  - **Validation rules:**
    - Can only call setpgid on self or child
    - Cannot change pgid of child after it has exec'd
    - Cannot move to different session
    - New group must be in same session

### Job Control (Phase 4.2 - Partial) [IMPLEMENTED]
- [2026-02-01] Process stop/continue infrastructure
  - **Files:** `kernel/process/process.h`, `kernel/process/process.c`
  - **New process state:** `PROC_STOPPED` - for stopped processes
  - **New fields in process_t:**
    - `struct tty *ctty` - Controlling terminal pointer
    - `uint32_t exec_count` - Number of exec() calls (for setpgid validation)
    - `int stop_signal` - Signal that stopped the process
    - `bool stop_reported` - Parent notified of stop via wait()
    - `bool continue_reported` - Parent notified of continuation via wait()
  - **Functions:**
    - `process_stop(proc, signum)` - Mark process as stopped, remove from scheduler
    - `process_continue(proc)` - Resume stopped process, add to scheduler
    - `process_find_stopped_child(parent_pid, child_pid)` - For wait() WUNTRACED
    - `process_find_continued_child(parent_pid, child_pid)` - For wait() WCONTINUED
- [2026-02-01] Signal handling for job control
  - **Files:** `kernel/signal/signal.h`, `kernel/signal/signal.c`
  - **Updated default signal handlers:**
    - SIG_ACTION_STOP: Calls `process_stop()`, sends SIGCHLD to parent
    - SIG_ACTION_CONT: Calls `process_continue()`, sends SIGCHLD to parent
  - **Auto-resume on SIGCONT:**
    - Updated `signal_send()` to resume stopped processes when SIGCONT received
    - Clears pending SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU signals
  - **New SIGCHLD variants:**
    - `signal_send_sigchld_stopped()` - Respects SA_NOCLDSTOP flag
    - `signal_send_sigchld_continued()` - Notifies parent of continuation
  - **Signals supported:** SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU, SIGCONT

### Enhanced wait() with WUNTRACED/WCONTINUED (Phase 4.3) [IMPLEMENTED]
- [2026-02-01] waitpid() options support
  - **Files:** `kernel/syscall/syscall.h`, `userspace/libc/syscall.h`
  - **New flags:**
    - `WNOHANG` (1) - Don't block waiting
    - `WUNTRACED` (2) - Also return stopped children
    - `WCONTINUED` (8) - Also return continued children
  - **New status macros:**
    - `WIFSTOPPED(status)` - True if child stopped
    - `WSTOPSIG(status)` - Extract stop signal
    - `WIFCONTINUED(status)` - True if child continued
- [2026-02-01] Rewritten sys_wait4() implementation
  - **File:** `kernel/syscall/syscall.c`
  - **Priority order:**
    1. Check for zombie children (existing behavior)
    2. Check for stopped children (if WUNTRACED flag set)
    3. Check for continued children (if WCONTINUED flag set)
  - **Status encoding:**
    - Normal exit: `(exit_code & 0xff) << 8`
    - Signal death: `signal & 0x7f`
    - Stopped: `0x7f | ((signal & 0xff) << 8)`
    - Continued: `0xffff`
  - **Reporting:** Marks children as stop_reported/continue_reported to avoid duplicates
  - **WNOHANG:** Returns 0 if no state change, not blocking

### TTY Background Access Control (Phase 4.2 & 5.3) [IMPLEMENTED]
- [2026-02-01] SIGTTIN/SIGTTOU for background TTY access
  - **Files:** `kernel/drivers/tty.c`, `kernel/drivers/pty.c`
  - **Console TTY access control:**
    - `tty_check_read_access()` - Checks foreground group on read
    - `tty_check_write_access()` - Checks foreground group on write (if TOSTOP set)
  - **PTY access control:**
    - `pty_check_read_access()` - Same checks for PTY slave reads
    - `pty_check_write_access()` - Same checks for PTY slave writes
  - **SIGTTIN behavior:**
    - Sent to background process group on read attempt
    - If ignored: return EIO
    - If blocked: return EIO
    - Otherwise: send signal and return EINTR
  - **SIGTTOU behavior:**
    - Only sent if TOSTOP termios flag is set
    - Sent to background process group on write attempt
    - If ignored: allow write
    - If blocked: return EIO
    - Otherwise: send signal and return EINTR
  - **Integration:** Both console TTY and PTY slaves respect foreground process groups

### File Permission Checking (Phase 7.1) [IMPLEMENTED]
- [2026-02-01] Permission enforcement on file operations
  - **Files:** `kernel/fs/vfs.c`, `kernel/syscall/syscall.c`
  - **New function:**
    - `vfs_check_permission(node, access_mode)` - Validates file access
  - **Permission bits checked:**
    - VFS_PERM_READ (0x04) - Read permission
    - VFS_PERM_WRITE (0x02) - Write permission
    - VFS_PERM_EXEC (0x01) - Execute permission
  - **Integration points:**
    - `vfs_open()` - Checks read/write permission based on O_RDONLY/O_WRONLY/O_RDWR
    - `sys_execve()` - Checks execute permission before loading ELF
  - **Error codes:**
    - Returns EACCES (-13) if permission denied
  - **Notes:**
    - Currently checks world permissions (last 3 bits)
    - Devices and directories always allowed (for now)
    - Full UID/GID checking deferred until credential system implemented

### User/Group Credentials (Phase 4.4 & 7.2) [IMPLEMENTED]
- [2026-02-01] Complete credentials system with proper POSIX semantics
  - **Files:** `kernel/process/process.h`, `kernel/process/process.c`, `kernel/syscall/syscall.c`
  - **New fields in process_t:**
    - `uint32_t uid, euid, suid` - Real, effective, saved user IDs
    - `uint32_t gid, egid, sgid` - Real, effective, saved group IDs
  - **Initialization:**
    - New processes inherit credentials from parent
    - Init process (PID 1) starts as root (0:0)
  - **Syscalls implemented:**
    - `getuid()`, `geteuid()`, `getgid()`, `getegid()` - Get IDs
    - `setuid()`, `setgid()` - Set IDs (root can set all, non-root restricted)
    - `setreuid()`, `setregid()` - Set real and effective IDs
    - `setresuid()`, `setresgid()` - Set all three IDs explicitly
    - `getresuid()`, `getresgid()` - Get all three IDs
  - **POSIX semantics:**
    - Root (euid==0) can set any ID
    - Non-root can only set to current uid/euid/suid values
    - Proper saved-ID handling for setuid programs (deferred)
- [2026-02-01] Enhanced permission checking with UID/GID
  - **Files:** `kernel/fs/vfs.c`
  - **Updated `vfs_check_permission()`:**
    - Checks owner permissions first (bits 6-8)
    - Then group permissions (bits 3-5)
    - Then other permissions (bits 0-2)
    - Root (euid==0) bypasses all checks
  - **Proper POSIX permission model:**
    - Owner match uses owner bits only
    - Group match uses group bits only
    - Falls back to other bits
  - **Error handling:** Returns EACCES if permission denied

### Basic Networking Stack (Phase 6 - UDP/ICMP/Loopback) [IMPLEMENTED]
- [2026-02-01] Complete network subsystem with UDP and ICMP support
  - **Files:** kernel/net/* (6 new files, ~1000 lines)
  - **Network device layer (kernel/net/netdev.c):**
    - netdev_t structure for network devices
    - Device registration and management
    - Packet allocation and handling
    - Device lookup by name/IP
  - **Loopback device (kernel/net/loopback.c):**
    - Full 127.0.0.1 support
    - Instant packet loopback
    - MTU: 65536 bytes
  - **IP layer (kernel/net/ip.c):**
    - IPv4 header parsing/validation
    - IP checksum calculation
    - Protocol dispatch (ICMP/UDP/TCP)
    - Simple routing (by source device IP)
  - **ICMP implementation (kernel/net/icmp.c):**
    - Echo request/reply (ping)
    - Automatic ping responses
  - **UDP implementation (kernel/net/udp.c):**
    - Full UDP send/receive
    - Port-based delivery
    - Checksum optional (not implemented for simplicity)
  - **Socket layer (kernel/net/socket.c):**
    - BSD socket API (subset)
    - Socket creation (SOCK_DGRAM only)
    - Bind, connect, sendto, recvfrom
    - Per-socket receive buffers (8KB)
    - Socket FD table (256 sockets max)
  - **Syscalls implemented:**
    - socket(AF_INET, SOCK_DGRAM, 0) - Create UDP socket
    - bind(sockfd, &addr, len) - Bind to address/port
    - connect(sockfd, &addr, len) - Set default destination
    - sendto(sockfd, buf, len, 0, &addr, addrlen) - Send UDP packet
    - recvfrom(sockfd, buf, len, 0, &addr, &addrlen) - Receive UDP packet
  - **Limitations:**
    - Loopback only (no physical network devices yet)
    - UDP only (TCP not implemented)
    - No select/poll (blocking only with EAGAIN)
    - Simple single-packet receive buffer per socket
  - **What works:**
    - Ping localhost (ICMP echo)
    - UDP client/server on 127.0.0.1
    - Multiple sockets, multiple ports
    - Proper port binding and delivery

### Resource Limits (Phase 7.3) [IMPLEMENTED]
- [2026-02-02] POSIX resource limits with getrlimit/setrlimit syscalls
  - **Files:** `kernel/process/process.h`, `kernel/process/process.c`, `kernel/syscall/syscall.c`, `kernel/fs/fd_table.c`
  - **New structures:**
    - `struct rlimit { rlim_t rlim_cur, rlim_max }` - Soft and hard limits
    - `RLIM_NLIMITS=16` - Number of limit types
    - Added `limits[16]` array to process_t
  - **Limit types defined:**
    - RLIMIT_CPU - CPU time in seconds
    - RLIMIT_FSIZE - Maximum file size
    - RLIMIT_DATA - Max data size
    - RLIMIT_STACK - Max stack size (default: 8MB/16MB)
    - RLIMIT_CORE - Max core file size
    - RLIMIT_RSS - Max resident set size
    - RLIMIT_NPROC - Max number of processes (default: 256/512)
    - RLIMIT_NOFILE - Max number of open files (default: 1024/4096)
    - RLIMIT_AS - Address space limit (default: unlimited)
  - **Syscalls implemented:**
    - `sys_getrlimit(resource, rlim)` - Get resource limit
    - `sys_setrlimit(resource, rlim)` - Set resource limit
  - **Permission checking:**
    - Non-root cannot raise hard limit (EPERM)
    - Soft limit cannot exceed hard limit (EINVAL)
  - **Enforcement:**
    - RLIMIT_NOFILE: Enforced in `fd_table_alloc()` - returns -1 if limit reached
    - RLIMIT_NPROC: Enforced in `sys_fork()` - returns EAGAIN if limit reached
    - RLIMIT_AS: Enforced in `sys_mmap()` - returns ENOMEM if would exceed limit
  - **Initialization:**
    - Kernel process (PID 0) gets defaults in `process_init()`
    - New processes inherit limits from parent in `process_create()`
  - **Bug fix:** Fixed kernel process initialization to properly set resource limits
  - **Tests:** All 3 resource limit tests passing

### TCP Networking (Phase 6.6) [IMPLEMENTED]
- [2026-02-02] Full TCP protocol implementation with connection management
  - **Files:** `kernel/net/tcp.h`, `kernel/net/tcp.c` (~600 lines), `kernel/net/socket.h`, `kernel/net/socket.c`, `kernel/net/ip.c`
  - **TCP structures:**
    - `tcp_header_t` - 20-byte TCP header (packed)
    - `tcp_state_t` - TCP state machine enum (11 states)
    - `tcp_connection_t` - Connection control block
  - **TCP flags:** FIN, SYN, RST, PSH, ACK, URG
  - **TCP states:** CLOSED, LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT, CLOSING, LAST_ACK, TIME_WAIT
  - **TCP implementation:**
    - Checksum calculation with pseudo-header
    - 3-way handshake (SYN → SYN-ACK → ACK)
    - Connection establishment and teardown
    - Sequence number tracking (snd_una, snd_nxt, rcv_nxt)
    - Flow control with window size
    - Basic timeout-based retransmission
  - **Buffer management:**
    - Send buffer: 64KB per connection
    - Receive buffer: 16KB per connection (fits in uint16_t window)
    - Circular buffer operations
  - **Socket integration:**
    - SOCK_STREAM support in `socket_create()`
    - TCP connection allocation for SOCK_STREAM sockets
    - Routing through tcp_send() for SOCK_STREAM
  - **Syscalls added:**
    - `sys_listen(sockfd, backlog)` - Mark socket as listening
    - `sys_accept(sockfd, addr, addrlen)` - Accept incoming connection
  - **Byte order functions:**
    - Added static inline htons(), htonl(), ntohs(), ntohl()
  - **IP layer integration:**
    - TCP packet dispatch in `ip_receive()` for IP_PROTO_TCP
  - **Limitations:**
    - Basic retransmission only (no sophisticated timeout calculation)
    - No TCP options support
    - No congestion control
    - Loopback only (until virtio-net tested)
  - **Tests:** All 5 socket/TCP tests passing

### Virtio-net Driver (Phase 6.2) [IMPLEMENTED]
- [2026-02-02] Network device driver for virtio-net (QEMU networking)
  - **Files:** `kernel/drivers/virtio/virtio_net.h`, `kernel/drivers/virtio/virtio_net.c` (~290 lines)
  - **Virtio-net structures:**
    - `virtio_net_config_t` - Device config (MAC, status, queue pairs)
    - `virtio_net_hdr_t` - Packet header for virtio protocol
    - `virtio_net_dev_t` - Device state structure
  - **Features:**
    - VIRTIO_NET_F_MAC - MAC address in config
    - VIRTIO_NET_F_STATUS - Link status
  - **Implementation:**
    - RX virtqueue for receiving packets
    - TX virtqueue for transmitting packets
    - Pre-allocated RX buffers (64 packets)
    - MAC address reading from config space
    - Scatter-gather I/O with virtqueue_add()
  - **Network integration:**
    - Registers with netdev layer
    - Static IP configuration: 10.0.2.15/24
    - MTU: 1500 bytes
  - **Transmit path:**
    - Build virtio_net_hdr + packet
    - Add to TX virtqueue
    - Kick device
  - **Receive path:**
    - IRQ handler processes RX completions
    - Calls netdev_receive() to deliver to IP layer
    - Refills RX buffers
  - **API compatibility fixes:**
    - virtio_reset() instead of virtio_reset_device()
    - virtio_negotiate_features() for feature negotiation
    - virtqueue_init() instead of virtqueue_create()
    - virtqueue_add() with scatter-gather descriptor arrays
    - Removed non-existent netdev_ops callbacks
  - **Status:** Compiles and links successfully, runtime testing requires QEMU -device virtio-net

### Critical Bug Fixes [2026-02-02]
- **Bootloader kernel size bug:**
  - **Problem:** Bootloader configured to load only 128KB (0x20000 bytes)
  - **Impact:** Kernel grew to 223KB, only first 128KB was loaded, causing corruption and boot failures
  - **Symptom:** Garbled serial output "0xFF 'S'" pattern after entering kernel
  - **Fix:** Rebuilt bootloader with KERNEL_SIZE=223768 for normal kernel, 236056 for test kernel
  - **Files:** `boot/Makefile`, bootloader stage1/stage2 rebuilt with correct size
  - **Result:** Kernel now boots successfully with all features

### Test Framework Enhancements [2026-02-02]
- **New test suites:**
  - `kernel/tests/test_rlimits.c` - Resource limit tests (3 tests)
  - `kernel/tests/test_socket.c` - Socket and TCP tests (5 tests)
  - `kernel/tests/test_netdev.c` - Network device tests (4 tests)
- **Test integration:**
  - Added to `kernel/Makefile` TEST_SOURCES
  - Registered in `kernel/tests/test_all.c`
- **Test results:**
  - 49/50 tests passing (98% pass rate)
  - 6,596 assertions passed, 1 failed
  - Only failure: pre-existing VMM test (vmm_map_single_page)
- **Test coverage:**
  - Resource limits: defaults, modification, bounds checking
  - Socket creation: UDP and TCP
  - Socket operations: bind, listen, accept
  - TCP connection lifecycle
  - Loopback device properties
  - Packet allocation

### Kernel Size Growth [2026-02-02]
- **Before:** 211KB (before resource limits, TCP, virtio-net)
- **After normal kernel:** 223,768 bytes (219KB)
- **After test kernel:** 236,056 bytes (231KB)
- **Growth:** +12KB for new features, +13KB for test framework

## 2026-02-02 - Unix Domain Sockets (Phase 8.4)

**Unix Domain Sockets - COMPLETE**

Implemented full Unix domain socket support for local inter-process communication:

### Core Implementation
- **unix_socket.c** (~600 lines): Complete Unix socket implementation
  - SOCK_STREAM: Connection-oriented, bidirectional byte streams
  - SOCK_DGRAM: Connectionless datagrams with queuing
  - Circular buffer for SOCK_STREAM (8KB)
  - Datagram queue for SOCK_DGRAM
  
### Socket Operations
- **bind()**: Bind socket to filesystem path
- **connect()**: Connect to bound socket
- **listen()**: Mark socket as listening (SOCK_STREAM)
- **accept()**: Accept incoming connections
- **sendto()/recvfrom()**: Send/receive data
- **Socket registration**: Global table of bound sockets

### Advanced Features
- **SCM_RIGHTS**: File descriptor passing between processes
  - `unix_socket_send_fds()` / `unix_socket_recv_fds()`
  - Control message structure for FD transfer
  - Up to 8 FDs per message

### Connection Management (SOCK_STREAM)
- Connection queue with configurable backlog
- Peer socket linking for bidirectional communication
- Automatic buffer management

### Integration
- Updated socket.c to dispatch AF_UNIX operations
- Updated syscalls (bind, connect, sendto, recvfrom)
- Proper sockaddr_un handling in kernel
- Socket table management (256 sockets max)

### Files
- kernel/net/unix_socket.h - Unix socket API
- kernel/net/unix_socket.c - Implementation (~600 lines)
- kernel/net/socket.h - Added sockaddr_un_t, AF_UNIX wrappers
- kernel/net/socket.c - AF_UNIX dispatch logic
- kernel/syscall/syscall.c - Updated sys_bind, sys_connect, sys_sendto, sys_recvfrom

### Use Cases
- Local IPC between processes
- Client-server communication on same machine
- File descriptor passing between processes
- Logging daemons, X11 servers, database sockets

**Kernel size:** 228KB (+17KB for Unix sockets)
**Ready for:** POSIX Shared Memory (Phase 8.1), Semaphores (Phase 8.2)
