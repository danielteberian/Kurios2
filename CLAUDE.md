# Claude Context for Kurios2

## Working Style
- **One task at a time** - Complete a single task, then wait for user verification
- **Incremental progress** - Small, testable changes
- **User verifies** before moving to next task
- **Update logs** - Keep TODO.md and COMPLETED.md current after each task

## Project Overview
- 64-bit x86_64 OS kernel
- Custom BIOS bootloader (stage1 + stage2)
- Higher-half kernel at virtual `0xFFFFFFFF80000000`
- Physical load address: `0x200000` (2MB aligned for 2MB pages)

## Key Files
| File | Purpose |
|------|---------|
| `boot/bios/stage1.asm` | MBR boot sector, loads stage2 |
| `boot/bios/stage2.asm` | A20, memory map, paging, mode switch, kernel load |
| `kernel/entry.asm` | Kernel entry, stack setup, BSS clear, calls kernel_main |
| `kernel/linker.ld` | Memory layout, VMA/LMA separation |
| `kernel/main.c` | Kernel init, prints boot info |
| `kernel/debug/debug.c` | kprintf, panic, logging |
| `kernel/arch/x86_64/serial.c` | COM1 serial driver |

## Memory Layout
- Kernel virtual base: `0xFFFFFFFF80000000`
- Kernel physical base: `0x200000`
- Boot info struct: `0x9000`
- Memory map: `0x9100`
- Page tables: `0x1000-0x9000`

## Build & Test
```bash
make clean && make all && make run-bios
```
Serial output visible in QEMU terminal via `-serial stdio`.

## Current Task
(Awaiting next task from user)

## Completed
- Bootloader (stage1 + stage2)
- Higher-half kernel paging
- Debug framework (serial, kprintf, panic)
- Boot info passing
- Global constructors/destructors
- DEBUG_TESTS conditional compilation
