# Kurios2 File Reference

> **Purpose**: Complete reference of every file in the project. Update this file whenever adding, removing, or significantly changing files.
>
> **Last Updated**: 2026-02-01

---

## Directory Structure Overview

```
Kurios2/
├── boot/                    # Bootloaders (BIOS and UEFI)
│   ├── bios/               # BIOS bootloader (stage1 + stage2)
│   ├── common/             # Shared boot structures
│   └── uefi/               # UEFI bootloader
├── docs/                    # Additional documentation
├── kernel/                  # Kernel source code
│   ├── acpi/               # ACPI table parsing
│   ├── apic/               # APIC interrupt controller
│   ├── arch/x86_64/        # x86_64-specific code
│   ├── debug/              # Debug infrastructure
│   ├── drivers/            # Device drivers
│   │   └── virtio/         # Virtio device drivers
│   ├── fs/                 # Filesystems
│   ├── include/            # Standard C headers
│   ├── initrd/             # Initial ramdisk
│   ├── lib/                # Library functions
│   ├── loader/             # ELF loader
│   ├── mm/                 # Memory management
│   ├── process/            # Process management
│   ├── sched/              # Scheduler and threads
│   ├── signal/             # Signal handling
│   ├── smp/                # Symmetric multiprocessing
│   ├── sync/               # Synchronization primitives
│   ├── syscall/            # System call interface
│   ├── tests/              # Kernel test framework
│   └── user/               # User-mode entry
└── toolchain/              # Build configuration
```

---

## Root Directory

| File | Purpose |
|------|---------|
| `Makefile` | Top-level build system. Targets: `all`, `run-bios`, `run-uefi`, `run-debug`, `run-gdb`, `clean`, `test` |
| `CLAUDE.md` | AI assistant context file. Contains project overview, key files, and current state |
| `TODO.md` | Comprehensive roadmap with 13 phases and ~150 tasks |
| `COMPLETED.md` | History of completed features with dates |
| `ARCHITECTURE.md` | High-level architecture documentation |
| `FILE_REFERENCE.md` | This file - complete file reference |

---

## boot/ - Bootloaders

### boot/bios/ - BIOS Bootloader

| File | Purpose |
|------|---------|
| `stage1.asm` | MBR boot sector (512 bytes). Loads stage2 from disk at 0x10000 |
| `stage2.asm` | Main bootloader (~17KB). Enables A20, gets memory map (E820), sets up paging, enters long mode, loads kernel at 0x200000, jumps to kernel |

### boot/common/ - Shared Boot Structures

| File | Purpose |
|------|---------|
| `boot_info.h` | C header for `BootInfo` structure passed to kernel. Contains memory map, kernel location, framebuffer info |
| `boot_info.inc` | NASM include with same structures for assembly code |

### boot/uefi/ - UEFI Bootloader

| File | Purpose |
|------|---------|
| `bootloader.c` | UEFI application. Uses UEFI services for memory map, loads kernel, exits boot services |
| `efi_types.h` | UEFI type definitions and protocol GUIDs |
| `uefi.ld` | Linker script for UEFI PE/COFF executable |

### boot/Makefile

Builds both BIOS and UEFI bootloaders. Supports `KERNEL_SIZE` and `INITRD_SIZE` overrides.

---

## kernel/ - Kernel Source

### kernel/ Root Files

| File | Purpose |
|------|---------|
| `main.c` | Kernel entry point (`kernel_main`). Initializes all subsystems in order, runs tests, enters keyboard loop |
| `entry.asm` | Assembly entry from bootloader. Sets up stack, clears BSS, calls global constructors, jumps to `kernel_main` |
| `linker.ld` | Kernel linker script. Higher-half at 0xFFFFFFFF80000000, VMA/LMA separation, init/fini arrays |
| `stack_protector.c` | Stack canary implementation with randomized value |
| `stack_protector.h` | Stack protector declarations |
| `Makefile` | Kernel build. Compiles all .c and .asm files, links kernel.elf, creates kernel.bin |

---

### kernel/include/ - Standard Headers

| File | Purpose |
|------|---------|
| `stdint.h` | Fixed-width integer types: `int8_t`, `uint64_t`, etc. |
| `stddef.h` | `size_t`, `NULL`, `offsetof` |
| `stdbool.h` | `bool`, `true`, `false` |
| `stdarg.h` | Variadic function macros: `va_list`, `va_start`, `va_arg`, `va_end` |
| `types.h` | Kernel type aliases, `UNUSED` macro |

---

### kernel/lib/ - Library Functions

| File | Purpose |
|------|---------|
| `string.c` | String/memory functions: `strlen`, `strcmp`, `strcpy`, `strncpy`, `strcat`, `strchr`, `strrchr`, `memcpy`, `memmove`, `memset`, `memcmp`, `atoi`, `itoa` |
| `string.h` | String function declarations |

---

### kernel/debug/ - Debug Infrastructure

| File | Purpose |
|------|---------|
| `debug.c` | Logging system: `kprintf`, `panic`, `ASSERT`. Log levels: TRACE, DEBUG, INFO, WARN, ERROR. Output to serial |
| `debug.h` | Debug macros: `INFO()`, `WARN()`, `ERROR()`, `DEBUG()`, `TRACE()`, `ASSERT()`, `panic()` |
| `gdb_stub.c` | GDB remote debugging via COM2. Implements RSP protocol, breakpoints, single-stepping, register read/write |
| `gdb_stub.h` | GDB stub interface: `gdb_init()`, `gdb_breakpoint()` |

---

### kernel/arch/x86_64/ - x86_64 Architecture

| File | Purpose |
|------|---------|
| `cpu.h` | CPU control: `read_cr0/2/3/4`, `write_cr*`, `read_msr`, `write_msr`, `cli`, `sti`, `hlt`, `invlpg` |
| `io.h` | Port I/O: `inb`, `outb`, `inw`, `outw`, `inl`, `outl`, `io_wait` |
| `gdt.c` | Global Descriptor Table setup. 7 entries: null, kernel code/data, user code/data, TSS. Per-CPU TSS with RSP0 and IST stacks |
| `gdt.h` | GDT structures and `gdt_init()` |
| `gdt_flush.asm` | `gdt_flush()` and `tss_flush()` - loads GDTR and TR |
| `idt.c` | Interrupt Descriptor Table. 256 entries, exception handlers (PF, GPF, DF), IRQ handlers, IST for critical exceptions |
| `idt.h` | IDT structures, `idt_init()`, IRQ constants |
| `isr.asm` | Interrupt service routine stubs. Saves/restores registers, calls C handlers |
| `serial.c` | COM1/COM2 serial driver. 115200 baud, polling mode |
| `serial.h` | Serial interface: `serial_init()`, `serial_putc()`, `serial_puts()`, `serial_getc()` |

---

### kernel/mm/ - Memory Management

| File | Purpose |
|------|---------|
| `pmm.c` | Physical Memory Manager. Buddy allocator with orders 0-10 (4KB-4MB). Uses page array for metadata. Handles memory map from bootloader |
| `pmm.h` | PMM interface: `alloc_page()`, `free_page()`, `alloc_pages()`, `free_pages()`, `mem_info` struct |
| `vmm.c` | Virtual Memory Manager. 4-level paging (PML4→PDPT→PD→PT). Supports 4KB, 2MB, 1GB pages. Maps kernel to higher half |
| `vmm.h` | VMM interface: `vmm_map_page()`, `vmm_unmap_page()`, `vmm_get_phys()`, `vmm_is_mapped()`, page table flags |
| `slab.c` | Slab allocator for kernel heap. 12 size classes (8B-4KB). Object caches with `kmem_cache_create/alloc/free` |
| `slab.h` | Slab interface: `kmalloc()`, `kfree()`, `krealloc()`, `kmem_cache_*` |
| `as.c` | Per-process address spaces. `as_create()`, `as_clone()`, `as_destroy()`, `as_switch()`. Clones page tables for fork() |
| `as.h` | Address space structures and interface |
| `vma.c` | Virtual Memory Areas. Tracks memory regions with start/end/flags. Used for demand paging and COW infrastructure |
| `vma.h` | VMA structures: `vma_t`, `vma_create()`, `vma_find()`, `vma_list()` |
| `fault.c` | Page fault handler (INT 14). Infrastructure for demand paging and COW (not fully implemented yet) |
| `fault.h` | Fault handler interface |
| `uaccess.c` | User-space memory access: `copy_to_user()`, `copy_from_user()`, `strncpy_from_user()` |
| `uaccess.h` | User access declarations |

---

### kernel/fs/ - Filesystems

| File | Purpose |
|------|---------|
| `vfs.c` | Virtual Filesystem layer. Mount table, path resolution, file operations. Uses slab caches for nodes/files/mounts |
| `vfs.h` | VFS structures: `vfs_node_t`, `vfs_mount_t`, `file_t`, `fs_ops_t`, `node_ops_t`. File operations: `vfs_open/read/write/close/stat` |
| `ramfs.c` | RAM filesystem. In-memory file storage with block arrays. Supports files and directories |
| `ramfs.h` | Ramfs interface: `ramfs_init()`, block size constants |
| `ext2.c` | ext2 filesystem driver (~1900 lines). Full read/write support. Block mapping (direct/indirect), bitmap allocation, directory operations |
| `ext2.h` | ext2 on-disk structures: superblock, group descriptors, inodes, directory entries. Constants for magic numbers, file types |
| `procfs.c` | /proc virtual filesystem. Provides: `/proc/version`, `/proc/meminfo`, `/proc/uptime`, `/proc/cpuinfo`, `/proc/stat` |
| `procfs.h` | Procfs interface: `procfs_mount()`, test functions |
| `pipe.c` | Pipe implementation. 4KB circular buffer, separate read/write ends, EAGAIN/EPIPE handling |
| `pipe.h` | Pipe interface: `pipe_create()` |
| `fd_table.c` | Per-process file descriptor tables. `fd_table_create/clone/destroy`, allocation, FD_CLOEXEC support |
| `fd_table.h` | FD table structures and interface |

---

### kernel/drivers/ - Device Drivers

| File | Purpose |
|------|---------|
| `keyboard.c` | PS/2 keyboard driver. IRQ1, scan code translation, US layout, modifier key tracking |
| `keyboard.h` | Keyboard interface: `keyboard_getchar()`, `keyboard_ctrl_pressed()`, key constants |
| `mouse.c` | PS/2 mouse driver. IRQ12, 3-byte packets, button state, movement events |
| `mouse.h` | Mouse interface: `mouse_init()`, event buffer |
| `vga.c` | VGA text mode driver. 80x25, color attributes, cursor control |
| `vga.h` | VGA interface: `vga_putc()`, `vga_puts()`, `vga_clear()`, color constants |
| `pit.c` | Programmable Interval Timer. 100Hz tick, `pit_get_ticks()`, `sleep_ms()` |
| `pit.h` | PIT interface and timing functions |
| `hpet.c` | High Precision Event Timer. 100MHz counter, `hpet_delay_us()`, `hpet_read_counter()` |
| `hpet.h` | HPET interface |
| `tty.c` | TTY driver for /dev/console. Line discipline, canonical mode, echo, signal characters (Ctrl+C/Z), termios support |
| `tty.h` | TTY interface: `tty_init()`, `tty_write()`, `tty_read()` |
| `pty.c` | Pseudo-terminal implementation. Master/slave pairs, /dev/ptmx, /dev/pts/* |
| `pty.h` | PTY interface: `pty_init()`, PTY ioctls |
| `termios.h` | Terminal I/O structures: `struct termios`, `c_iflag`, `c_oflag`, `c_cflag`, `c_lflag`, `c_cc` |
| `pci.c` | PCI bus enumeration. Config space read/write, device discovery, BAR access |
| `pci.h` | PCI structures: `pci_device_t`, class codes, config offsets |
| `block.c` | Block device abstraction. `block_device_t`, sector read/write, device registration |
| `block.h` | Block device interface and structures |

### kernel/drivers/virtio/ - Virtio Drivers

| File | Purpose |
|------|---------|
| `virtio.c` | Virtio core. Virtqueue management, legacy PCI transport, feature negotiation |
| `virtio.h` | Virtio structures: `virtqueue_t`, device types, feature bits |
| `virtio_blk.c` | Virtio block device driver. Registers as block device, polling-based I/O |

---

### kernel/acpi/ - ACPI Support

| File | Purpose |
|------|---------|
| `acpi.c` | ACPI table parsing. Finds RSDP, parses RSDT/XSDT, extracts MADT (APIC info), FADT, HPET tables |
| `acpi.h` | ACPI structures: RSDP, RSDT, MADT, FADT. `acpi_init()`, accessor functions |

---

### kernel/apic/ - APIC Interrupt Controller

| File | Purpose |
|------|---------|
| `apic.c` | Local APIC and I/O APIC initialization. Disables legacy PIC, configures IRQ routing, LAPIC timer |
| `apic.h` | APIC interface: `apic_init()`, `apic_eoi()`, `lapic_timer_init()`, IPI functions |

---

### kernel/sched/ - Scheduler

| File | Purpose |
|------|---------|
| `thread.c` | Thread management. TCB allocation, `thread_create()`, `thread_exit()`, `thread_yield()`, `thread_sleep_ms()` |
| `thread.h` | Thread structures: `thread_t`, states, priorities |
| `sched.c` | Round-robin scheduler. 100ms time slices, run queue, `sched_start()`, `sched_yield()` |
| `sched.h` | Scheduler interface |
| `context.asm` | Context switch. Saves/restores callee-saved registers (RBX, RBP, R12-R15), switches stack |

---

### kernel/process/ - Process Management

| File | Purpose |
|------|---------|
| `process.c` | Process table, PID allocation, `process_create()`, `process_exit()`, `process_current()`. Kernel process is PID 0 |
| `process.h` | Process structures: `process_t`, states, `process_init()` |

---

### kernel/syscall/ - System Calls

| File | Purpose |
|------|---------|
| `syscall.c` | System call handlers. 30+ syscalls: exit, read, write, open, close, fork, exec, wait, getpid, brk, mmap, stat, pipe, dup, signal, etc. |
| `syscall.h` | Syscall numbers, handler prototypes |
| `syscall_entry.asm` | SYSCALL/SYSRET entry point. Saves user state, calls C handler, restores state |

---

### kernel/signal/ - Signal Handling

| File | Purpose |
|------|---------|
| `signal.c` | Signal infrastructure. `signal_deliver_pending()`, `sigaction`, `sigreturn`, SIGCHLD on child exit |
| `signal.h` | Signal numbers, structures: `sigaction_t`, `sigset_t`, `siginfo_t` |

---

### kernel/smp/ - Symmetric Multiprocessing

| File | Purpose |
|------|---------|
| `percpu.c` | Per-CPU data management. BSP initialization, AP allocation |
| `percpu.h` | Per-CPU structures, `percpu_get()`, `percpu_set()` |
| `smp.c` | SMP initialization. AP boot sequence, CPU enumeration |
| `smp.h` | SMP interface: `smp_init()`, `smp_cpu_count()`, IPI sending |
| `ap_trampoline.asm` | AP startup code. Real mode → protected mode → long mode |
| `tlb.c` | TLB shootdown implementation. IPI-based invalidation |
| `tlb.h` | TLB interface: `tlb_shootdown()` |

---

### kernel/initrd/ - Initial Ramdisk

| File | Purpose |
|------|---------|
| `initrd.c` | CPIO newc format parser. Extracts files to ramfs at boot |
| `initrd.h` | Initrd interface: `initrd_init()`, `initrd_mount()`, `initrd_available()` |

---

### kernel/loader/ - ELF Loading

| File | Purpose |
|------|---------|
| `elf.h` | ELF64 structures: `Elf64_Ehdr`, `Elf64_Phdr`, segment types, flags |
| `elf_loader.c` | ELF executable loader. Validates headers, loads PT_LOAD segments, handles BSS |
| `elf_loader.h` | Loader interface: `elf_load()` |

---

### kernel/user/ - User-Mode Support

| File | Purpose |
|------|---------|
| `user_entry.c` | Test user-mode program. Enters ring 3, makes syscalls |
| `user_entry.h` | User entry interface |
| `ring3_enter.asm` | Enters user mode via IRETQ with proper segment selectors |

---

### kernel/sync/ - Synchronization

| File | Purpose |
|------|---------|
| `spinlock.h` | IRQ-safe spinlocks. `spin_lock_irqsave()`, `spin_unlock_irqrestore()`, `spin_init()` |

---

### kernel/tests/ - Test Framework

| File | Purpose |
|------|---------|
| `test_framework.c` | Test harness. Test registration, running, result reporting |
| `test_framework.h` | Test macros: `TEST_ASSERT`, `TEST_CASE`, `TEST_SUITE` |
| `tests.h` | Test suite declarations |
| `test_all.c` | Runs all registered test suites |
| `test_pmm.c` | PMM unit tests |
| `test_vmm.c` | VMM unit tests |
| `test_slab.c` | Slab allocator tests |
| `test_spinlock.c` | Spinlock tests |

---

## docs/ - Documentation

| File | Purpose |
|------|---------|
| `IMPLEMENTATION_GUIDES.md` | Detailed implementation guides for complex features |
| `USERSPACE_CHECKLIST.md` | Comprehensive checklist for user-space (Phase 12): libc, coreutils, shell, etc. |

---

## toolchain/ - Build Configuration

| File | Purpose |
|------|---------|
| `config.mk` | Compiler and linker flags, tool paths. Defines CC, LD, AS, CFLAGS, LDFLAGS |

---

## Memory Layout

```
Physical Memory:
0x00000000 - 0x00001000  : Reserved (real mode IVT, BDA)
0x00001000 - 0x00009000  : Boot page tables
0x00009000 - 0x00009100  : BootInfo structure
0x00009100 - 0x00010000  : Memory map entries
0x00010000 - 0x00020000  : Stage 2 bootloader
0x00200000 - 0x00400000  : Kernel (loaded here, up to 2MB)
0x00400000+              : Free memory (managed by PMM)

Virtual Memory (Higher Half):
0xFFFFFFFF80000000       : Kernel base (maps to physical 0x200000)
0xFFFFFFFF90000000+      : Dynamic kernel mappings (APIC, HPET, etc.)
```

---

## Build Targets

```bash
make                    # Build bootloader and kernel
make run-bios          # Run in QEMU with BIOS
make run-uefi          # Run in QEMU with UEFI
make run-debug         # Build with DEBUG_TESTS, run in QEMU
make run-gdb           # Run with GDB support (COM2 = TCP:1234)
make test              # Build and run kernel tests
make clean             # Remove all build artifacts
```

---

## Adding New Files

When adding new files:
1. Add .c files to `kernel/Makefile` C_SOURCES
2. Add .asm files to `kernel/Makefile` ASM_SOURCES
3. Create directory in $(BUILD_DIR) if new subdirectory
4. **Update this FILE_REFERENCE.md**
5. Update CLAUDE.md Key Files table if it's a major component

---

## File Statistics

- **Total source files**: ~100
- **Kernel C files**: ~55
- **Kernel ASM files**: 7
- **Header files**: ~45
- **Kernel size**: ~165KB
