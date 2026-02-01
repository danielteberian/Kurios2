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
9. [x] ACPI table parsing (MADT, FADT, HPET)
10. [x] APIC setup (Local APIC + I/O APIC, legacy PIC disabled)
11. [x] PIT timer (100Hz tick, sleep_ms)
12. [x] HPET timer (100MHz, delay functions)
13. [x] Basic spinlocks/mutexes (IRQ-safe)

### Boot Protocol
14. [ ] Multiboot2 support (alternative to custom bootloader)

### Process Management
15. [x] Multithreading (kernel threads, round-robin scheduler, context switching)
16. [x] Process structure and process table (PID 0-255, state machine)
17. [x] Per-process address spaces (as_create, as_clone, as_switch)
18. [x] Syscall infrastructure (SYSCALL/SYSRET, syscall table)
19. [x] User-space entry (ring 3 via IRETQ, TSS.RSP0)
20. [x] ELF loader (PT_LOAD segments)
21. [x] fork() system call (address space + fd table cloning)
22. [x] exec() system call (ELF loading, address space replacement)

### Device Drivers
23. [x] VGA text mode driver
24. [x] Keyboard driver (PS/2, IRQ1, US layout, extended keys)
25. [x] TTY (/dev/console - VGA output, keyboard input buffer)
26. [x] /dev/null, /dev/zero, /dev/urandom
27. [x] Mouse driver (PS/2, IRQ12, 3-byte packets)

### Filesystem
28. [x] VFS layer (mount, file/dir operations)
29. [x] Ramfs (in-memory filesystem)
30. [x] Initial ramdisk (CPIO newc parser, ramfs mounting)
31. [x] Procfs (/proc/version, meminfo, uptime, cpuinfo, stat)
32. [x] Per-process fd tables (fork clones, exec closes FD_CLOEXEC)
33. [ ] Filesystem driver (ext2, FAT32, or custom)

### IPC & Signals
34. [x] Pipes (4KB circular buffer, sys_pipe)
35. [x] Signal delivery (sigaction, sigreturn, SIGCHLD, user handlers)

### Syscalls
36. [x] File I/O: read, write, open, close, lseek, stat, fstat, access
37. [x] Directories: mkdir, rmdir, chdir, fchdir, getcwd, getdents, rename, unlink, readlink
38. [x] FD operations: dup, dup2, fcntl (F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL), pipe, ioctl
39. [x] Memory: brk, mmap, mprotect, munmap
40. [x] Signals: sigaction, sigprocmask, sigreturn, kill
41. [x] Process: fork, execve, exit, getpid, getppid, wait4
42. [x] Time: gettimeofday, clock_gettime, clock_getres, nanosleep
43. [x] IDs: getuid, getgid, geteuid, getegid, setuid, setgid
44. [x] Sessions: setsid, getsid, getpgid, setpgid
45. [x] Misc: uname, syslog, umask, truncate, ftruncate, sched_yield, alarm
46. [x] Stubs: socket, connect, accept, sendto, recvfrom, bind, listen, getrlimit, setrlimit, link, symlink, poll, select

### Debugging
47. [x] GDB Stub (remote debugging via COM2, breakpoints, single-stepping)

---

## In Progress
- None

## Next Up
- [ ] **SMP support** - Multi-core CPU support (plan exists in .claude/plans/)
- [ ] **User-space shell** - Simple /bin/sh
- [ ] **PTY** - Pseudo-terminals for remote shells
