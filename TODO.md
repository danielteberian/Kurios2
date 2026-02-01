# Kurios2 - 64-bit OS Kernel TODO

## Kernel Foundation (in order)

### Core Foundation
0. [x] Testing and debug framework (serial output, assertions, panic handler)
1. [x] Higher-half kernel + updated linker script
2. [x] Global constructors/destructors
3. [x] Stack smash protector
4. [x] Physical memory manager (buddy allocator)
5. [x] Virtual memory manager (page table management, map/unmap API)
6. [x] Kernel heap (slab allocator - kmalloc/kfree)
7. [x] GDT with TSS (kernel/user segments, per-CPU TSS)
8. [x] IDT + exception handlers (page fault, GPF, double fault, etc.)

### Hardware Abstraction
8. [x] ACPI table parsing (find MADT, HPET, FADT)
9. [x] APIC setup (Local APIC + I/O APIC, replace legacy PIC)
10. [x] Timer (PIT at 100Hz)
11. [x] Basic spinlocks/mutexes

### Boot Protocol
12. [ ] Multiboot2 support (alternative to custom bootloader)

### Process Management
13. [x] Multithreading (kernel threads, scheduler, context switching)
14. [x] Process structure and process table (kernel processes, PID management)
15. [x] Per-process address spaces (separate page tables)
16. [x] Syscall infrastructure (SYSCALL/SYSRET, syscall table)
17. [x] User-space entry (ring 3 transition)
18. [x] fork() system call
19. [x] exec() system call

### Device Drivers
15. [x] Keyboard driver (PS/2 keyboard via IRQ1)
16. [ ] Additional input devices (mouse, etc.)

### Filesystem
17. [x] VFS layer (virtual filesystem abstraction) - COMPLETE
18. [x] Ramfs (in-memory filesystem) - COMPLETE
19. [x] Initial ramdisk (initrd/initramfs) - COMPLETE
20. [ ] Filesystem driver (ext2, FAT32, or custom)

## In Progress
- None

## Next Up
- [ ] **SMP support** - Multi-core CPU support (uses APIC)
- [ ] **User-space shell** - Simple /bin/sh
- [ ] **PTY** - Pseudo-terminals for remote shells

## Completed Recently
- [x] **fchdir syscall** - IMPLEMENTED (change directory by fd, builds path from node)
- [x] **umask syscall** - IMPLEMENTED (per-process file creation mask, default 022)
- [x] **fcntl syscall** - IMPLEMENTED (F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL)
- [x] **/dev/urandom** - IMPLEMENTED (xorshift64 PRNG seeded from TSC)
- [x] **isatty/ioctl** - IMPLEMENTED (TCGETS, TIOCGWINSZ for TTY detection)
- [x] **/dev/null, /dev/zero** - IMPLEMENTED (null device, zero device)
- [x] **Standard I/O** - IMPLEMENTED (fd 0/1/2 auto-connected to /dev/console)
- [x] **TTY** - IMPLEMENTED (/dev/console, VGA output, keyboard input buffer)
- [x] **Pipes** - IMPLEMENTED (pipe_create, read/write circular buffer, sys_pipe)
- [x] **Signal delivery** - IMPLEMENTED (signal_deliver_pending, sigreturn, SIGCHLD, user handler invocation)
- [x] **Procfs filesystem** - VERIFIED WORKING (/proc/version, /proc/meminfo, /proc/uptime, /proc/cpuinfo, /proc/stat)
- [x] **Extended syscalls** - VERIFIED WORKING (30+ syscalls: uname, gettimeofday, clock_gettime, brk, mmap, sigaction, getpgid, etc.)
- [x] File I/O syscalls - IMPLEMENTED (read/write for VFS files, lseek, fstat, dup, dup2)
- [x] Initial ramdisk (initrd) - VERIFIED WORKING (CPIO newc parser, ramfs mounting)
- [x] HPET timer - VERIFIED WORKING (100 MHz, 3 timers, 64-bit counter, delay functions)
- [x] APIC setup - VERIFIED WORKING (Local APIC, I/O APIC, IRQ routing, PIC disabled)
- [x] ACPI table parsing - VERIFIED WORKING (MADT, FADT, HPET; Local APIC, I/O APIC, IRQ overrides)
- [x] Per-process file descriptor table - IMPLEMENTED (fd_table.c, fork clones fds, exec closes FD_CLOEXEC)
- [x] exec() system call - IMPLEMENTED (sys_execve_impl, ELF loading, address space replacement)
- [x] fork() system call - IMPLEMENTED (sys_fork_impl, address space cloning)
- [x] User-space entry (ring 3) - IMPLEMENTED (IRETQ, TSS.RSP0)
- [x] Syscall infrastructure - VERIFIED WORKING (SYSCALL/SYSRET, syscall table)
- [x] ELF loader - VERIFIED WORKING (load PT_LOAD segments)
- [x] Per-process address spaces - VERIFIED WORKING (as_clone, as_create)
- [x] Process structure and process table - VERIFIED WORKING (10 tests, PID 0-255)
- [x] VFS + Ramfs - VERIFIED WORKING (file ops, directory ops, mounting)
- [x] GDB Stub - VERIFIED WORKING (remote debugging over serial)
- [x] Multithreading - VERIFIED WORKING (round-robin scheduler, context switching, preemption)
- [x] PIT Timer (100Hz) - VERIFIED WORKING (uptime, sleep_ms)
- [x] VGA text mode driver - VERIFIED WORKING
- [x] Keyboard driver (PS/2) - VERIFIED WORKING (IRQ1, US layout, extended keys)
- [x] Basic spinlocks/mutexes - VERIFIED WORKING (4 tests)
- [x] IDT + Exception Handlers - VERIFIED WORKING
- [x] GDT with TSS - VERIFIED WORKING
- [x] Slab Allocator (kmalloc/kfree) - VERIFIED WORKING (14 tests)
- [x] VMM (Virtual Memory Manager) - VERIFIED WORKING
- [x] PMM (Buddy Allocator) - VERIFIED WORKING
- [x] Stack smash protector - VERIFIED WORKING
- [x] Global constructors/destructors - VERIFIED WORKING
- [x] Debug framework + Higher-half kernel - VERIFIED WORKING
