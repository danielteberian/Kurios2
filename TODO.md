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

## Phase 1: Memory Management Improvements [x]

### 1.1 Demand Paging [x]
- [x] Page fault handler for lazy allocation
- [x] Allocate physical pages on first access (not at mmap time)
- [x] Track mapped vs committed pages in address space
- [x] Handle stack growth via guard pages

### 1.2 Copy-on-Write (COW) [x]
- [x] Mark shared pages read-only on fork()
- [x] COW page fault handler (copy page, make writable)
- [x] Reference counting for shared physical pages
- [x] Proper cleanup when last reference dropped

### 1.3 Memory-Mapped Files [x]
- [x] mmap() with file backing (MAP_PRIVATE)
- [x] mmap() with file backing (MAP_SHARED)
- [x] Page cache integration
- [x] msync() for flushing changes
- [x] munmap() with proper cleanup

### 1.4 Swap (Optional - Low Priority)
- [ ] Swap partition/file support
- [ ] Page eviction policy (LRU or clock)
- [ ] Swap in/out mechanisms
- [ ] Swap space management

---

## Phase 2: Block Device Layer [x]

### 2.1 Block Device Abstraction [x]
- [x] block_device_t structure (sector size, count, ops)
- [x] Block device registration API
- [x] Sector read/write interface
- [x] Block request queue (async I/O)

### 2.2 I/O Scheduler [x]
- [x] Request merging
- [x] Simple elevator algorithm (or NOOP)
- [x] Async I/O completion callbacks

### 2.3 Partition Support [x]
- [x] MBR partition table parsing
- [x] GPT partition table parsing
- [x] Partition device creation (vda1, vda2, etc.)

### 2.4 Storage Drivers [PARTIAL]
- [x] Virtio-blk driver (for QEMU, polling mode)
- [ ] ATA/IDE driver (PIO mode)
- [ ] AHCI/SATA driver (optional, complex)

### 2.5 Buffer/Page Cache [PARTIAL]
- [x] Block buffer cache (page cache for file-backed mmap)
- [x] LRU eviction policy (infrastructure ready)
- [x] Dirty buffer writeback (via msync)
- [x] sync() syscall

---

## Phase 3: Real Filesystems

### 3.1 FAT32 Filesystem [x]
- [x] FAT32 superblock (BPB) parsing
- [x] FAT table reading/caching (single-sector cache)
- [x] Directory entry parsing (8.3 and LFN)
- [x] File read support
- [x] File write support
- [x] Directory operations (mkdir, rmdir)
- [x] File creation/deletion

### 3.2 ext2 Filesystem [x]
- [x] Superblock and group descriptor parsing
- [x] Inode reading/writing
- [x] Block allocation (bitmap)
- [x] Inode allocation (bitmap)
- [x] Directory operations (readdir, finddir, mkdir, rmdir)
- [x] File read/write (direct + indirect blocks)
- [x] Symbolic links (fast + slow symlinks)

### 3.3 Devfs Improvements [x]
- [x] Dynamic device node creation (vfs_mknod)
- [x] Major/minor number support (dev_t, MAJOR/MINOR macros)
- [x] mknod() syscall (SYS_MKNOD=133)
- [x] Device file permissions (stored in node->permissions)

---

## Phase 4: Process Model Enhancements [PARTIAL]

### 4.1 Process Groups and Sessions [x]
- [x] Process group signal broadcast (pgrp_send_signal)
- [x] Process group validation (pgrp_validate_setpgid)
- [x] Process group existence checking (pgrp_exists)
- [x] Orphaned process group detection (pgrp_is_orphaned)
- [x] Enhanced kill() for process groups (pid=0, pid<-1)
- [x] POSIX setpgid() validation
- [~] Controlling terminal association (TIOCSCTTY ioctl - needs TTY integration)
- [~] Foreground process group tracking (infrastructure ready)
- [~] SIGHUP on session leader death (infrastructure ready)

### 4.2 Job Control [x]
- [x] PROC_STOPPED state for stopped processes
- [x] SIGTSTP (Ctrl+Z) handling via process_stop()
- [x] SIGCONT for resuming stopped processes via process_continue()
- [x] SIGCHLD notification on stop/continue
- [x] Auto-resume stopped processes on SIGCONT
- [x] Orphaned process group handling
- [x] SIGTTIN/SIGTTOU for background I/O
- [x] TTY access control checks (tty_check_read_access/tty_check_write_access)

### 4.3 wait() Improvements [x]
- [x] waitpid() with WUNTRACED flag
- [x] waitpid() with WCONTINUED flag
- [x] Status encoding for stopped processes (0x7f | (signal << 8))
- [x] Status encoding for continued processes (0xffff)
- [x] WIFSTOPPED/WSTOPSIG/WIFCONTINUED macros
- [x] Proper stop_reported/continue_reported tracking
- [x] wait4() with rusage parameter (rusage still ignored)
- [x] Proper zombie reaping
- [ ] init (PID 1) adopts orphans (low priority)

### 4.4 Credentials [x]
- [x] Real/effective/saved UID/GID
- [x] setuid()/setgid() proper implementation
- [x] setreuid()/setregid()
- [x] setresuid()/setresgid()
- [x] getresuid()/getresgid()
- [ ] Supplementary groups (getgroups/setgroups) - deferred

---

## Phase 5: TTY Subsystem

### 5.1 Line Discipline [x]
- [x] Canonical mode (line buffering)
- [x] Raw mode (character-by-character)
- [x] Echo control
- [x] Special character handling (^C, ^Z, ^D, ^U, ^W)
- [x] termios structure and operations
- [x] tcgetattr()/tcsetattr() via ioctl

### 5.2 PTY (Pseudo-Terminals) [x]
- [x] PTY master/slave pair creation
- [x] /dev/ptmx multiplexer device
- [x] /dev/pts/* slave devices
- [x] PTY-specific ioctls (TIOCGPTN, TIOCSPTLCK)
- [ ] posix_openpt(), grantpt(), unlockpt(), ptsname() (user-space wrappers)

### 5.3 Terminal Signals [x]
- [x] SIGINT on ^C (to foreground pgrp)
- [x] SIGQUIT on ^\ (to foreground pgrp)
- [x] SIGTSTP on ^Z (to foreground pgrp)
- [x] SIGTTIN/SIGTTOU for background access

---

## Phase 6: Networking Stack [COMPLETE]

### 6.1 Network Device Layer [x]
- [x] netdev_t structure
- [x] Network device registration
- [x] Packet receive path
- [x] Packet transmit path

### 6.2 Network Drivers [PARTIAL]
- [x] Virtio-net driver (for QEMU)
- [ ] E1000 driver (Intel NIC, also QEMU) - deferred
- [x] Loopback device (127.0.0.1)

### 6.3 Ethernet/ARP [SKIPPED]
- [ ] Ethernet frame parsing - not needed for loopback
- [ ] ARP request/reply - not needed for loopback
- [ ] ARP cache - not needed for loopback

### 6.4 IP Layer [x]
- [x] IPv4 header parsing/creation
- [x] IP routing table (simple - by device IP)
- [x] ICMP (ping support - echo request/reply)
- [ ] IP fragmentation (optional) - deferred

### 6.5 UDP [x]
- [x] UDP socket implementation
- [x] UDP send/receive
- [x] Port binding

### 6.6 TCP [x]
- [x] TCP state machine
- [x] Connection establishment (3-way handshake)
- [x] Data transfer with sequence numbers
- [x] Flow control (sliding window)
- [x] Connection termination
- [x] Retransmission (basic timeout-based)

### 6.7 Socket API [PARTIAL]
- [x] socket() - create socket (AF_INET, SOCK_DGRAM and SOCK_STREAM)
- [x] bind() - bind to address
- [x] listen() - mark as listening (TCP)
- [x] accept() - accept connection (TCP)
- [x] connect() - connect to remote (TCP and UDP)
- [ ] send()/recv() - data transfer (use sendto/recvfrom)
- [x] sendto()/recvfrom() - UDP and TCP
- [ ] select()/poll() - I/O multiplexing - deferred
- [ ] getsockopt()/setsockopt() - deferred
- [ ] getpeername()/getsockname() - deferred

### 6.8 DNS (Optional)
- [ ] DNS query construction
- [ ] DNS response parsing
- [ ] /etc/resolv.conf parsing
- [ ] gethostbyname() support

---

## Phase 7: Security Model [COMPLETE]

### 7.1 File Permissions [x]
- [x] Permission bits in inodes (rwxrwxrwx)
- [x] Owner/group in inodes
- [x] Permission checking on open/exec
- [x] umask support (already stubbed)
- [x] chmod()/fchmod() implementation
- [x] chown()/fchown() implementation

### 7.2 Process Credentials [x]
- [x] UID/GID checking on file access
- [ ] setuid/setgid bit handling on exec (deferred)
- [ ] Capability system (optional, Linux-style)

### 7.3 Resource Limits [x]
- [x] rlimit structure and values
- [x] getrlimit()/setrlimit() implementation
- [x] RLIMIT_NOFILE (max open files) - enforced in fd_table_alloc()
- [x] RLIMIT_AS (address space) - enforced in sys_mmap()
- [x] RLIMIT_STACK (stack size) - default 8MB/16MB
- [x] RLIMIT_CPU (CPU time) - structure defined
- [x] RLIMIT_NPROC (max processes) - enforced in sys_fork()

---

## Phase 8: IPC Enhancements [COMPLETE]

### 8.1 POSIX Shared Memory [x]
- [x] shm_open()/shm_unlink()
- [x] Shared memory object management
- [x] Physical memory allocation and kernel mapping
- [x] Reference counting for shared segments

### 8.2 POSIX Semaphores [x]
- [x] sem_open()/sem_close()/sem_unlink()
- [x] sem_wait()/sem_post()/sem_trywait()
- [x] sem_getvalue()
- [x] Named semaphores
- [x] Wait queues with blocking/unblocking

### 8.3 POSIX Message Queues [x]
- [x] mq_open()/mq_close()/mq_unlink()
- [x] mq_send()/mq_receive()
- [x] Message priority support
- [x] mq_getattr()/mq_setattr()
- [x] Priority-ordered message delivery
- [x] Blocking send/receive with wait queues

### 8.4 Unix Domain Sockets [x]
- [x] AF_UNIX socket creation
- [x] bind() to filesystem path
- [x] SOCK_STREAM (connection-oriented)
- [x] SOCK_DGRAM (connectionless)
- [x] SCM_RIGHTS (fd passing)

---

## Phase 9: Dynamic Linking [CORE COMPLETE]

### 9.1 ELF Interpreter Support [x]
- [x] PT_INTERP segment handling
- [x] Load ELF interpreter (ld.so) via elf_load_file()
- [x] Auxiliary vector (AT_*) passing (AT_PHDR, AT_ENTRY, AT_BASE, AT_RANDOM, AT_UID/GID, AT_PAGESZ)
- [x] Interpreter gets control first (entry point override)

### 9.2 Shared Library Support [PARTIAL]
- [x] PT_DYNAMIC segment parsing (recorded for ld.so)
- [ ] DT_NEEDED (library dependencies) - delegated to ld.so
- [ ] Library search path (/lib, /usr/lib) - delegated to ld.so
- [ ] Shared library loading - delegated to ld.so

### 9.3 Relocation [PARTIAL]
- [x] R_X86_64_RELATIVE (basic PIE support)
- [ ] PLT (Procedure Linkage Table) - delegated to ld.so
- [ ] GOT (Global Offset Table) - delegated to ld.so
- [ ] Lazy binding support - delegated to ld.so
- [ ] Other R_X86_64_* relocation types (GLOB_DAT, JUMP_SLOT, etc.) - delegated to ld.so

### 9.4 Symbol Resolution [NOT STARTED]
- [ ] Symbol table parsing (.dynsym) - delegated to ld.so
- [ ] String table parsing (.dynstr) - delegated to ld.so
- [ ] Symbol lookup across libraries - delegated to ld.so
- [ ] Weak symbols - delegated to ld.so

**Architecture Note**: Core kernel provides PT_INTERP loading, auxiliary vector, and basic relocations.
Complex relocations, symbol resolution, and library loading are delegated to the dynamic linker (ld.so).

---

## Phase 10: SMP Improvements [COMPLETE]

### 10.1 AP Boot [x]
- [x] Debug and fix AP startup (fixed physical vs virtual address bug)
- [x] AP enters long mode and runs scheduler
- [x] Per-CPU idle threads (each CPU has its own idle thread)
- [ ] CPU hotplug (optional)

### 10.2 SMP Synchronization [x]
- [x] Read-write locks (rwlock) - kernel/sync/rwlock.c
- [x] Per-CPU variables (percpu_data infrastructure)
- [ ] RCU (Read-Copy-Update) - optional, advanced

### 10.3 TLB Management [x]
- [x] TLB shootdown (infrastructure exists via kernel/smp/tlb.c)
- [x] Test and verify TLB shootdown works (verified in earlier session)
- [x] Optimize for single-CPU case (checks smp_initialized())

### 10.4 Scheduler Improvements [x]
- [x] Per-CPU run queues (each CPU has its own ready queue)
- [x] Load balancing between CPUs (automatic migration every 1 second)
- [x] CPU affinity (sched_setaffinity/getaffinity syscalls, affinity-aware scheduling)
- [ ] Real-time scheduling classes (optional)

**Status**: All essential SMP features complete. Optional items: CPU hotplug, RCU, real-time scheduling.

---

## Phase 11: Power Management [PARTIAL]

### 11.1 ACPI Power States [PARTIAL]
- [ ] S0 (working) state management
- [ ] S3 (suspend to RAM) - optional
- [x] S5 (soft off) - shutdown via ACPI PM1a_CNT
- [ ] Power button event handling

### 11.2 CPU Power Management
- [ ] C-states (idle power saving)
- [ ] P-states (frequency scaling) - optional
- [x] HLT instruction in idle loop (already done)

---

## Phase 12: User-Space Support

> **NOTE**: Goal is a self-hosting OS for development work.
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

## Phase 13: Miscellaneous [PARTIAL]

### 13.1 Time Management [x]
- [x] Wall clock time (RTC reading)
- [x] settimeofday() implementation (root only)
- [ ] Timezone support (/etc/localtime)
- [x] time() syscall (via gettimeofday/clock_gettime)

### 13.2 Random Number Generator [x]
- [x] Entropy pool (512-byte pool with TSC/PIT/RTC/HPET sources)
- [ ] /dev/random (blocking) - deferred
- [x] /dev/urandom improvements (entropy pool with mixing)
- [x] getrandom() syscall

### 13.3 Kernel Modules (Optional - Advanced)
- [ ] Module loading infrastructure
- [ ] Module symbol resolution
- [ ] Module unloading
- [ ] insmod/rmmod/lsmod utilities

### 13.4 Kernel Command Line [x]
- [x] Parse kernel command line from bootloader
- [x] cmdline_get_param() API
- [x] cmdline_has_flag() API
- [x] Common options infrastructure (ready for root=, init=, console=)

### 13.5 System Control
- [x] reboot() syscall (POWER_OFF, HALT, RESTART support)
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
1. [x] COW fork() - Phase 1.2
2. [x] Demand paging - Phase 1.1
3. [x] Line discipline - Phase 5.1
4. [x] PTY - Phase 5.2
5. [x] Block device layer - Phase 2.1-2.2
6. [x] Virtio-blk driver - Phase 2.4
7. [x] ext2 filesystem - Phase 3.2
8. [x] Partition support - Phase 2.3
9. [x] FAT32 filesystem - Phase 3.1

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

### Phase 12 - User-Space - See Phase 12 for full checklists
- [ ] C library (libc) + math library (libm)
- [x] Shell (/bin/sh) - Basic shell with built-ins (cd, pwd, ls, cat, echo, mkdir, rm)
- [ ] Coreutils, fileutils, binutils
- [ ] Development tools (compiler, make, editor)
- [ ] Init system

---

## Notes

### Project Goal
**Self-hosting development OS** - An OS for development and programming work, including libc, shell, utilities, and development tools.

### Architecture Decisions Pending
- Filesystem: Both FAT32 and ext2 implemented!
- Network driver: virtio-net (simpler) vs e1000 (more portable)?
- Libc: Custom minimal vs port musl/newlib?

### Known Issues
- SMP: AP boot hangs after SIPI (needs debugging)

### Testing Needed
- Stress test allocations (PMM/slab)
- Multi-process scenarios
- Signal edge cases
- Full user-space program execution

---

## Completed Phases
- [x] Phase 0: Core Foundation (see COMPLETED.md for details)
- [x] Phase 1: Memory Management Improvements (demand paging, COW, file mmap)
- [x] Phase 2: Block Device Layer (async I/O, I/O scheduler, virtio-blk)
