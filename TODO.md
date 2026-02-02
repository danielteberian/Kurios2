# Kurios2 - 64-bit OS Kernel TODO

## Legend
- `[ ]` Not started
- `[~]` In progress
- `[x]` Completed
- `[S]` Stub only (returns ENOSYS or minimal implementation)

---

## Phase 0: Core Foundation [COMPLETE]

### Memory Management [x]
- [x] Physical memory manager (buddy allocator, 4KB-4MB blocks)
- [x] Virtual memory manager (4-level paging, 4KB/2MB/1GB pages)
- [x] Kernel heap (slab allocator, kmalloc/kfree)

### CPU/Interrupts [x]
- [x] GDT with TSS (kernel/user segments, per-CPU TSS)
- [x] IDT + exception handlers (PF, GPF, DF, etc.)
- [x] ACPI table parsing (MADT, FADT, HPET)
- [x] Local APIC + I/O APIC (legacy PIC disabled)
- [x] PIT timer (100Hz tick)
- [x] HPET timer (100MHz precision)

### Process Model [x]
- [x] Thread management (TCB, create/exit/yield)
- [x] Round-robin scheduler (100ms slices)
- [x] Context switching (callee-saved registers)
- [x] Process structure (PID, state, resources)
- [x] Per-process address spaces (as_create/clone/destroy)
- [x] SYSCALL/SYSRET infrastructure
- [x] Ring 3 entry (IRETQ)
- [x] ELF loader (PT_LOAD segments)
- [x] fork() syscall (address space + fd table clone)
- [x] exec() syscall (ELF load, address space replace)

### Filesystem [x]
- [x] VFS layer (mount, file/dir operations)
- [x] Ramfs (in-memory filesystem)
- [x] Initrd (CPIO newc parser)
- [x] Procfs (/proc virtual filesystem)
- [x] Per-process fd tables

### IPC & Signals [x]
- [x] Pipes (4KB circular buffer)
- [x] Signal delivery (sigaction, sigreturn, user handlers)
- [x] SIGCHLD on child exit

### Drivers [x]
- [x] Serial (COM1/COM2)
- [x] VGA text mode
- [x] PS/2 keyboard (IRQ1)
- [x] PS/2 mouse (IRQ12)
- [x] TTY (/dev/console)
- [x] /dev/null, /dev/zero, /dev/urandom

### Debug [x]
- [x] kprintf, panic, assertions
- [x] GDB stub (COM2, breakpoints, single-step)
- [x] Stack smash protector

---

## Phase 1: Memory Management Improvements

### 1.1 Demand Paging
- [ ] Page fault handler for lazy allocation
- [ ] Allocate physical pages on first access (not at mmap time)
- [ ] Track mapped vs committed pages in address space
- [ ] Handle stack growth via guard pages

### 1.2 Copy-on-Write (COW)
- [ ] Mark shared pages read-only on fork()
- [ ] COW page fault handler (copy page, make writable)
- [ ] Reference counting for shared physical pages
- [ ] Proper cleanup when last reference dropped

### 1.3 Memory-Mapped Files
- [ ] mmap() with file backing (MAP_PRIVATE)
- [ ] mmap() with file backing (MAP_SHARED)
- [ ] Page cache integration
- [ ] msync() for flushing changes
- [ ] munmap() with proper cleanup

### 1.4 Swap (Optional - Low Priority)
- [ ] Swap partition/file support
- [ ] Page eviction policy (LRU or clock)
- [ ] Swap in/out mechanisms
- [ ] Swap space management

---

## Phase 2: Block Device Layer [PARTIAL]

### 2.1 Block Device Abstraction [x]
- [x] block_device_t structure (sector size, count, ops)
- [x] Block device registration API
- [x] Sector read/write interface
- [ ] Block request queue (async I/O)

### 2.2 I/O Scheduler
- [ ] Request merging
- [ ] Simple elevator algorithm (or NOOP)
- [ ] Async I/O completion callbacks

### 2.3 Partition Support
- [ ] MBR partition table parsing
- [ ] GPT partition table parsing
- [ ] Partition device creation (/dev/sda1, etc.)

### 2.4 Storage Drivers [PARTIAL]
- [x] Virtio-blk driver (for QEMU, polling mode)
- [ ] ATA/IDE driver (PIO mode)
- [ ] AHCI/SATA driver (optional, complex)

### 2.5 Buffer/Page Cache
- [ ] Block buffer cache
- [ ] LRU eviction policy
- [ ] Dirty buffer writeback
- [ ] sync() syscall

---

## Phase 3: Real Filesystems

### 3.1 FAT32 Filesystem
- [ ] FAT32 superblock parsing
- [ ] FAT table reading/caching
- [ ] Directory entry parsing (8.3 and LFN)
- [ ] File read support
- [ ] File write support
- [ ] Directory operations (mkdir, rmdir)
- [ ] File creation/deletion

### 3.2 ext2 Filesystem [x]
- [x] Superblock and group descriptor parsing
- [x] Inode reading/writing
- [x] Block allocation (bitmap)
- [x] Inode allocation (bitmap)
- [x] Directory operations (readdir, finddir, mkdir, rmdir)
- [x] File read/write (direct + indirect blocks)
- [x] Symbolic links (fast + slow symlinks)

### 3.3 Devfs Improvements
- [ ] Dynamic device node creation
- [ ] Major/minor number support
- [ ] mknod() syscall
- [ ] Device file permissions

---

## Phase 4: Process Model Enhancements

### 4.1 Process Groups and Sessions
- [ ] Process group leadership (setpgid already exists)
- [ ] Session leadership (setsid already exists)
- [ ] Controlling terminal association
- [ ] Foreground process group tracking
- [ ] SIGHUP on session leader death

### 4.2 Job Control
- [ ] SIGTSTP (Ctrl+Z) handling
- [ ] SIGCONT for resuming stopped processes
- [ ] SIGTTIN/SIGTTOU for background I/O
- [ ] wait() with WUNTRACED, WCONTINUED
- [ ] Orphaned process group handling

### 4.3 wait() Improvements
- [ ] waitpid() with options (WNOHANG, WUNTRACED, WCONTINUED)
- [ ] wait4() with rusage (already stubbed)
- [ ] Proper zombie reaping
- [ ] init (PID 1) adopts orphans

### 4.4 Credentials
- [ ] Real/effective/saved UID/GID
- [ ] setuid()/setgid() proper implementation
- [ ] setreuid()/setregid()
- [ ] setresuid()/setresgid()
- [ ] Supplementary groups (getgroups/setgroups)

---

## Phase 5: TTY Subsystem

### 5.1 Line Discipline
- [ ] Canonical mode (line buffering)
- [ ] Raw mode (character-by-character)
- [ ] Echo control
- [ ] Special character handling (^C, ^Z, ^D, ^U, ^W)
- [ ] termios structure and operations
- [ ] tcgetattr()/tcsetattr() via ioctl

### 5.2 PTY (Pseudo-Terminals) [x]
- [x] PTY master/slave pair creation
- [x] /dev/ptmx multiplexer device
- [x] /dev/pts/* slave devices
- [x] PTY-specific ioctls (TIOCGPTN, TIOCSPTLCK)
- [ ] posix_openpt(), grantpt(), unlockpt(), ptsname() (user-space wrappers)

### 5.3 Terminal Signals
- [ ] SIGINT on ^C (to foreground pgrp)
- [ ] SIGQUIT on ^\ (to foreground pgrp)
- [ ] SIGTSTP on ^Z (to foreground pgrp)
- [ ] SIGTTIN/SIGTTOU for background access

---

## Phase 6: Networking Stack

### 6.1 Network Device Layer
- [ ] netdev_t structure
- [ ] Network device registration
- [ ] Packet receive path
- [ ] Packet transmit path

### 6.2 Network Drivers
- [ ] Virtio-net driver (for QEMU)
- [ ] E1000 driver (Intel NIC, also QEMU)
- [ ] Loopback device (127.0.0.1)

### 6.3 Ethernet/ARP
- [ ] Ethernet frame parsing
- [ ] ARP request/reply
- [ ] ARP cache

### 6.4 IP Layer
- [ ] IPv4 header parsing/creation
- [ ] IP routing table (simple)
- [ ] ICMP (ping support)
- [ ] IP fragmentation (optional)

### 6.5 UDP
- [ ] UDP socket implementation
- [ ] UDP send/receive
- [ ] Port binding

### 6.6 TCP
- [ ] TCP state machine
- [ ] Connection establishment (3-way handshake)
- [ ] Data transfer with sequence numbers
- [ ] Flow control (sliding window)
- [ ] Connection termination
- [ ] Retransmission

### 6.7 Socket API
- [ ] socket() - create socket
- [ ] bind() - bind to address
- [ ] listen() - mark as listening
- [ ] accept() - accept connection
- [ ] connect() - connect to remote
- [ ] send()/recv() - data transfer
- [ ] sendto()/recvfrom() - UDP
- [ ] select()/poll() - I/O multiplexing
- [ ] getsockopt()/setsockopt()
- [ ] getpeername()/getsockname()

### 6.8 DNS (Optional)
- [ ] DNS query construction
- [ ] DNS response parsing
- [ ] /etc/resolv.conf parsing
- [ ] gethostbyname() support

---

## Phase 7: Security Model

### 7.1 File Permissions
- [x] Permission bits in inodes (rwxrwxrwx)
- [x] Owner/group in inodes
- [ ] Permission checking on open/exec
- [x] umask support (already stubbed)
- [x] chmod()/fchmod() implementation
- [x] chown()/fchown() implementation

### 7.2 Process Credentials
- [ ] UID/GID checking on file access
- [ ] setuid/setgid bit handling on exec
- [ ] Capability system (optional, Linux-style)

### 7.3 Resource Limits
- [ ] rlimit structure and values
- [ ] getrlimit()/setrlimit() implementation
- [ ] RLIMIT_NOFILE (max open files)
- [ ] RLIMIT_AS (address space)
- [ ] RLIMIT_STACK (stack size)
- [ ] RLIMIT_CPU (CPU time)
- [ ] RLIMIT_NPROC (max processes)

---

## Phase 8: IPC Enhancements

### 8.1 POSIX Shared Memory
- [ ] shm_open()/shm_unlink()
- [ ] Shared memory object in tmpfs/shmfs
- [ ] mmap() shared memory objects
- [ ] Reference counting for shared segments

### 8.2 POSIX Semaphores
- [ ] sem_open()/sem_close()/sem_unlink()
- [ ] sem_wait()/sem_post()/sem_trywait()
- [ ] Named semaphores (via filesystem)
- [ ] Unnamed semaphores (in shared memory)

### 8.3 POSIX Message Queues (Optional)
- [ ] mq_open()/mq_close()/mq_unlink()
- [ ] mq_send()/mq_receive()
- [ ] Message priority support

### 8.4 Unix Domain Sockets
- [ ] AF_UNIX socket creation
- [ ] bind() to filesystem path
- [ ] SOCK_STREAM (connection-oriented)
- [ ] SOCK_DGRAM (connectionless)
- [ ] SCM_RIGHTS (fd passing)

---

## Phase 9: Dynamic Linking

### 9.1 ELF Interpreter Support
- [ ] PT_INTERP segment handling
- [ ] Load ELF interpreter (ld.so)
- [ ] Auxiliary vector (AT_*) passing
- [ ] Interpreter gets control first

### 9.2 Shared Library Support
- [ ] PT_DYNAMIC segment parsing
- [ ] DT_NEEDED (library dependencies)
- [ ] Library search path (/lib, /usr/lib)
- [ ] Shared library loading

### 9.3 Relocation
- [ ] PLT (Procedure Linkage Table)
- [ ] GOT (Global Offset Table)
- [ ] Lazy binding support
- [ ] R_X86_64_* relocation types

### 9.4 Symbol Resolution
- [ ] Symbol table parsing (.dynsym)
- [ ] String table parsing (.dynstr)
- [ ] Symbol lookup across libraries
- [ ] Weak symbols

---

## Phase 10: SMP Improvements

### 10.1 AP Boot
- [ ] Debug and fix AP startup (currently hangs after SIPI)
- [ ] AP enters long mode and runs scheduler
- [ ] Per-CPU idle threads
- [ ] CPU hotplug (optional)

### 10.2 SMP Synchronization
- [ ] Read-write locks (rwlock)
- [ ] Per-CPU variables (already have infrastructure)
- [ ] RCU (Read-Copy-Update) - optional, advanced

### 10.3 TLB Management
- [ ] TLB shootdown (infrastructure exists)
- [ ] Test and verify TLB shootdown works
- [ ] Optimize for single-CPU case

### 10.4 Scheduler Improvements
- [ ] Per-CPU run queues
- [ ] Load balancing between CPUs
- [ ] CPU affinity (sched_setaffinity)
- [ ] Real-time scheduling classes (optional)

---

## Phase 11: Power Management

### 11.1 ACPI Power States
- [ ] S0 (working) state management
- [ ] S3 (suspend to RAM) - optional
- [ ] S5 (soft off) - shutdown
- [ ] Power button event handling

### 11.2 CPU Power Management
- [ ] C-states (idle power saving)
- [ ] P-states (frequency scaling) - optional
- [ ] HLT instruction in idle loop (already done)

---

## Phase 12: User-Space Support [USER-OWNED]

> **NOTE**: Phase 12 is **USER-OWNED** (off-limits unless explicitly requested).
> Goal: Self-hosting OS for development work.
>
> **See `docs/USERSPACE_CHECKLIST.md` for the comprehensive checklist.**

### Summary
- **C Library (libc)** - ~400 functions across 25+ headers
- **Math Library (libm)** - ~150 functions (float/double/long double variants)
- **Shell (/bin/sh)** - POSIX-compatible shell
- **Coreutils** - ~80 utilities
- **Binutils** - as, ld, ar, nm, objdump, readelf, strip, etc.
- **Text Editors** - ed, vi
- **Init System** - init, shutdown, login, getty
- **Development Tools** - compiler, make, debugger

---

## Phase 13: Miscellaneous

### 13.1 Time Management
- [x] Wall clock time (RTC reading)
- [ ] settimeofday() implementation
- [ ] Timezone support (/etc/localtime)
- [x] time() syscall (via gettimeofday/clock_gettime)

### 13.2 Random Number Generator
- [ ] Entropy pool
- [ ] /dev/random (blocking)
- [ ] /dev/urandom improvements (better PRNG)
- [x] getrandom() syscall

### 13.3 Kernel Modules (Optional - Advanced)
- [ ] Module loading infrastructure
- [ ] Module symbol resolution
- [ ] Module unloading
- [ ] insmod/rmmod/lsmod utilities

### 13.4 Kernel Command Line
- [ ] Parse kernel command line from bootloader
- [ ] cmdline_get_param() API
- [ ] Common options (root=, init=, console=)

### 13.5 Kernel Updates & Live Patching
- [ ] USB mass storage driver for update media
- [ ] Network-based kernel updates (HTTP/TFTP)
- [ ] Kernel image verification (signatures/checksums)
- [ ] Live patching infrastructure (function replacement)
- [ ] Ftrace-style function hooking for hot patches
- [ ] Rollback mechanism for failed patches
- [ ] kexec() for seamless kernel replacement
- [ ] Update daemon/service for automated updates

---

## Quick Reference: Priority Tasks

### High Priority (Foundation)
1. [ ] COW fork() - Phase 1.2
2. [ ] Demand paging - Phase 1.1
3. [ ] Line discipline - Phase 5.1
4. [ ] PTY - Phase 5.2
5. [ ] Block device layer - Phase 2.1-2.2
6. [ ] Virtio-blk driver - Phase 2.4
7. [x] ext2 filesystem - Phase 3.2

### Medium Priority (Usability)
8. [ ] Process groups/job control - Phase 4.1-4.2
9. [ ] File permissions - Phase 7.1
10. [ ] Unix domain sockets - Phase 8.4
11. [ ] Basic networking (loopback + UDP) - Phase 6

### Lower Priority (Polish)
12. [ ] TCP networking - Phase 6.6
13. [ ] Dynamic linking - Phase 9
14. [ ] SMP improvements - Phase 10
15. [ ] Kernel modules - Phase 13.3

### User-Owned (Phase 12) - See Phase 12 for full checklists
- [ ] C library (libc) + math library (libm)
- [x] Shell (/bin/sh) - Basic shell with built-ins (cd, pwd, ls, cat, echo, mkdir, rm)
- [ ] Coreutils, fileutils, binutils
- [ ] Development tools (compiler, make, editor)
- [ ] Init system

---

## Notes

### Project Goal
**Self-hosting development OS** - An OS for development and programming work. Phase 12 (user-space: libc, shell, utilities, tools) is user-owned and off-limits unless explicitly requested.

### Architecture Decisions Pending
- Filesystem: FAT32 (simpler) vs ext2 (more Unix-like)?
- Network driver: virtio-net (simpler) vs e1000 (more portable)?
- Libc: Custom minimal vs port musl/newlib?

### Known Issues
- SMP: AP boot hangs after SIPI (needs debugging)
- fork(): Full copy instead of COW (inefficient)
- mmap(): Anonymous only, no file backing

### Testing Needed
- Stress test allocations (PMM/slab)
- Multi-process scenarios
- Signal edge cases
- Full user-space program execution

---

## Completed Phases
- [x] Phase 0: Core Foundation (see COMPLETED.md for details)
