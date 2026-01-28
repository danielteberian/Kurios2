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
