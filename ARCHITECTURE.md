# Kurios2 Architecture Documentation

## Overview

Kurios2 is a 64-bit x86_64 operating system kernel with a custom hybrid bootloader supporting both BIOS and UEFI boot methods. The kernel runs as a **higher-half kernel** at virtual address `0xFFFFFFFF80000000`.

## Project Structure

```
Kurios2/
├── boot/                    # Bootloader sources
│   ├── bios/               # BIOS bootloader
│   │   ├── stage1.asm      # Boot sector (MBR)
│   │   └── stage2.asm      # Second stage loader
│   ├── uefi/               # UEFI bootloader
│   │   ├── bootloader.c    # UEFI application
│   │   └── uefi.ld         # UEFI linker script
│   ├── common/             # Shared boot definitions
│   │   ├── boot_info.h     # C header for boot info
│   │   └── boot_info.inc   # ASM include for boot info
│   └── Makefile
├── kernel/                  # Kernel sources
│   ├── arch/x86_64/        # Architecture-specific code
│   │   ├── cpu.h           # CPU operations (CR access, MSRs, etc.)
│   │   ├── io.h            # I/O port access
│   │   ├── serial.h        # Serial port header
│   │   └── serial.c        # Serial port driver
│   ├── debug/              # Debug/logging framework
│   │   ├── debug.h         # Debug API header
│   │   └── debug.c         # kprintf, panic, assertions
│   ├── include/            # Kernel headers
│   │   ├── types.h         # Common types and macros
│   │   ├── stdint.h        # Integer types
│   │   ├── stddef.h        # Standard definitions
│   │   ├── stdbool.h       # Boolean type
│   │   └── stdarg.h        # Variadic arguments
│   ├── entry.asm           # Kernel entry point
│   ├── main.c              # Kernel main function
│   ├── linker.ld           # Kernel linker script
│   └── Makefile
├── toolchain/              # Build toolchain
│   ├── config.mk           # Compiler/linker flags
│   ├── build-cross-compiler.sh
│   └── verify.sh
├── build/                  # Build outputs (generated)
├── Makefile                # Top-level build system
├── TODO.md                 # Task tracking
└── COMPLETED.md            # Completed work log
```

---

## File Descriptions

### Bootloader Files

#### `boot/bios/stage1.asm`
**Purpose:** First-stage bootloader (MBR boot sector)

- Loaded by BIOS at `0x7C00`
- 512 bytes exactly (ends with `0xAA55` signature)
- Initializes segments and stack
- Loads Stage 2 from disk (32 sectors starting at sector 2)
- Jumps to Stage 2 at `0x1000:0x0000` (linear `0x10000`)
- Passes boot drive number in `DL`

#### `boot/bios/stage2.asm`
**Purpose:** Second-stage bootloader - prepares system for 64-bit kernel

**Responsibilities:**
1. **A20 Line Enablement** - Tries BIOS, Fast A20, and keyboard controller methods
2. **E820 Memory Map** - Gets memory layout via BIOS INT 15h
3. **Kernel Loading** - Uses extended BIOS read to load kernel to 2MB
4. **Mode Transitions:**
   - Real Mode (16-bit) → Protected Mode (32-bit) → Long Mode (64-bit)
5. **Paging Setup:**
   - Identity maps first 4GB using 2MB pages
   - Creates higher-half mapping: `0xFFFFFFFF80000000` → physical `0x200000`
6. **Boot Info Preparation** - Fills BootInfo structure at `0x9000`
7. **Kernel Handoff** - Jumps to kernel at virtual address with RDI = boot_info pointer

**Key Memory Layout:**
- Page tables at `0x1000-0x9000`
- Boot info at `0x9000`
- Memory map at `0x9100`
- Temporary kernel load buffer at `0x20000`
- Final kernel location at `0x200000` (physical)

#### `boot/common/boot_info.inc`
**Purpose:** Assembly include file defining boot protocol constants

- Structure offsets for BootInfo
- Memory map entry format (E820 compatible)
- Boot flags (BIOS, UEFI, framebuffer, ACPI)
- Memory type constants
- Standard memory addresses

#### `boot/common/boot_info.h`
**Purpose:** C header for boot info structure (matches ASM definitions)

- `BootInfo` structure definition
- `MemoryMapEntry` structure
- `FramebufferInfo` structure
- Magic number: `0x4B55524953` ("KURIS")

---

### Kernel Files

#### `kernel/entry.asm`
**Purpose:** Kernel entry point (first code executed in kernel)

**What it does:**
1. Saves boot_info pointer from RDI to R15 (critical!)
2. Sets up kernel stack at `stack_top` (64KB in BSS)
3. Clears BSS section
4. Restores boot_info pointer to RDI
5. Calls `kernel_main(boot_info)`
6. Halts if kernel_main returns

**Important Note:** In 64-bit mode, writing to EDI zero-extends and destroys RDI. The entry point must preserve RDI in a callee-saved register (R15) before any operations that might touch EDI.

#### `kernel/linker.ld`
**Purpose:** Kernel linker script defining memory layout

**Key Addresses:**
- `KERNEL_VIRT_BASE = 0xFFFFFFFF80000000` (higher-half)
- `KERNEL_PHYS_BASE = 0x200000` (2MB, aligned for 2MB pages)
- `KERNEL_OFFSET = VIRT - PHYS` (for address translation)

**Sections:**
- `.boot` - Early boot code (if needed for pre-paging)
- `.text` - Code
- `.rodata` - Read-only data
- `.data` - Initialized data
- `.bss` - Uninitialized data (zeroed by entry.asm)

**Exported Symbols:**
- `_kernel_start`, `_kernel_end`
- `_kernel_phys_start`, `_kernel_phys_end`
- `_bss_start`, `_bss_end`
- `_kernel_virt_base`, `_kernel_phys_base`

#### `kernel/main.c`
**Purpose:** Kernel main function - initialization and testing

**What it does:**
1. Initializes debug subsystem (serial output)
2. Validates boot_info magic number
3. Prints boot information (flags, protocol version)
4. Prints kernel location info
5. Prints CPU state (CR0, CR3, CR4, RIP)
6. Verifies higher-half execution (checks RIP >= 0xFFFFFFFF80000000)
7. Prints memory map from E820
8. Tests debug framework (log levels, assertions)
9. Halts in infinite loop

---

### Architecture-Specific Files

#### `kernel/arch/x86_64/cpu.h`
**Purpose:** x86_64 CPU operations and register access

**Provides:**
- `cpu_state_t` structure for exception handling
- Control register access: `read_cr0/2/3/4()`, `write_cr0/3/4()`
- MSR access: `read_msr()`, `write_msr()`
- Common MSR definitions (EFER, FS/GS base, APIC base)
- Interrupt control: `cli()`, `sti()`, `hlt()`
- CPUID wrapper functions
- TLB flush: `invlpg()`, `flush_tlb()`
- Timestamp counter: `rdtsc()`, `rdtscp()`

#### `kernel/arch/x86_64/io.h`
**Purpose:** x86_64 I/O port access functions

**Provides:**
- Byte I/O: `inb()`, `outb()`
- Word I/O: `inw()`, `outw()`
- Dword I/O: `inl()`, `outl()`
- String I/O: `insb/w/l()`, `outsb/w/l()`
- `io_wait()` for legacy device timing

#### `kernel/arch/x86_64/serial.h` / `serial.c`
**Purpose:** Serial port (UART 16550) driver

**Features:**
- Supports COM1-COM4
- Configurable baud rate (default 115200)
- Blocking putc/getc operations
- Loopback self-test on init
- 8N1 configuration (8 data bits, no parity, 1 stop bit)

**Functions:**
- `serial_init()`, `serial_init_default()`
- `serial_putc()`, `serial_getc()`
- `serial_puts()`, `serial_write()`
- `debug_putc()`, `debug_puts()` (COM1 convenience)

---

### Debug Framework

#### `kernel/debug/debug.h` / `debug.c`
**Purpose:** Comprehensive kernel debugging and logging framework

**Features:**
- `kprintf()` - printf-like formatted output
- Log levels: TRACE, DEBUG, INFO, WARN, ERROR, FATAL
- ANSI color output for serial terminal
- `panic()` - kernel panic with stack trace
- `ASSERT()` / `ASSERT_MSG()` macros
- `hex_dump()` - memory dump utility
- `stack_trace()` - call stack unwinding
- `dump_registers()` - CPU state display

**Log Macros:**
```c
TRACE("message");   // Detailed tracing
DEBUG("message");   // Debug info
INFO("message");    // General info
WARN("message");    // Warnings
ERROR("message");   // Non-fatal errors
FATAL("message");   // Fatal (triggers panic)
```

---

### Include Files

#### `kernel/include/types.h`
**Purpose:** Common kernel type definitions and macros

**Types:**
- `phys_addr_t`, `virt_addr_t` - Address types
- `size_t`, `ssize_t` - Size types

**Constants:**
- `PAGE_SIZE` (4KB), `PAGE_SIZE_2M`, `PAGE_SIZE_1G`

**Macros:**
- `ALIGN_UP()`, `ALIGN_DOWN()`, `IS_ALIGNED()`
- `MIN()`, `MAX()`, `ARRAY_SIZE()`
- `BIT()`, `SET_BIT()`, `CLEAR_BIT()`, `TEST_BIT()`
- Memory barriers: `barrier()`, `mb()`, `rmb()`, `wmb()`

---

## Higher-Half Kernel Design

### Memory Map

| Virtual Address | Physical Address | Size | Description |
|----------------|------------------|------|-------------|
| 0x0000000000000000 | 0x00000000 | 4GB | Identity mapped (bootloader) |
| 0xFFFFFFFF80000000 | 0x00200000 | 16MB | Kernel code/data |

### Address Translation

```
Virtual 0xFFFFFFFF80000000 → Physical 0x200000
Virtual 0xFFFFFFFF80001000 → Physical 0x201000
...
```

### Page Table Structure (4-level paging)

```
PML4[0]   → PDPT at 0x2000   → Identity map first 4GB
PML4[511] → PDPT at 0x7000   → Higher-half mapping
  └─ PDPT[510] → PD at 0x8000
       └─ PD[0-7] → 2MB pages starting at physical 0x200000
```

### Why 0x200000?

The kernel is loaded at 2MB physical (not 1MB) because:
1. 2MB pages require 2MB-aligned physical addresses
2. Using 2MB pages simplifies the page table setup
3. Avoids the legacy low memory region (< 1MB)

---

## Build System

### Build Commands

```bash
make all        # Build bootloader and kernel
make boot       # Build bootloader only
make kernel     # Build kernel only
make image-bios # Create BIOS disk image
make run-bios   # Run in QEMU with BIOS
make clean      # Remove build artifacts
```

### Compiler Flags (from toolchain/config.mk)

- `-m64` - 64-bit code
- `-ffreestanding` - No standard library
- `-fno-stack-protector` - No stack canaries
- `-mno-red-zone` - Disable red zone (required for kernel)
- `-mcmodel=kernel` - Kernel memory model (addresses above 2GB)
- `-fno-pic` - No position-independent code

---

## Boot Sequence

1. **BIOS loads Stage 1** at 0x7C00
2. **Stage 1 loads Stage 2** at 0x10000
3. **Stage 2:**
   - Enables A20 line
   - Gets E820 memory map
   - Loads kernel to 0x200000
   - Enters protected mode
   - Sets up paging (identity + higher-half)
   - Enables long mode
   - Jumps to kernel at 0xFFFFFFFF80000000
4. **Kernel entry.asm:**
   - Sets up stack
   - Clears BSS
   - Calls kernel_main()
5. **kernel_main():**
   - Initializes debug output
   - Validates boot info
   - Prints system information
   - Halts

---

## Current Status

### Completed
- Custom BIOS bootloader (stage1 + stage2)
- Mode transitions (real → protected → long)
- Higher-half paging setup
- Kernel entry and BSS initialization
- Serial port debug output
- kprintf and logging framework
- Boot info passing

### Next Steps (from TODO.md)
- GDT with TSS
- IDT + exception handlers
- Physical memory manager
- Virtual memory manager
- Kernel heap
