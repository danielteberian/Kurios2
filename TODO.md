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
8. [ ] ACPI table parsing (find MADT, HPET, FADT)
9. [ ] APIC setup (Local APIC + I/O APIC, replace legacy PIC)
10. [x] Timer (PIT at 100Hz)
11. [x] Basic spinlocks/mutexes

### Boot Protocol
12. [ ] Multiboot2 support (alternative to custom bootloader)

### Process Management
13. [x] Multithreading (kernel threads, scheduler, context switching)
14. [x] Process structure and process table (kernel processes, PID management)
15. [ ] Per-process address spaces (separate page tables)
16. [ ] Syscall infrastructure (SYSCALL/SYSRET, syscall table)
17. [ ] User-space entry (ring 3 transition)

### Device Drivers
15. [x] Keyboard driver (PS/2 keyboard via IRQ1)
16. [ ] Additional input devices (mouse, etc.)

### Filesystem
17. [x] VFS layer (virtual filesystem abstraction) - COMPLETE
18. [x] Ramfs (in-memory filesystem) - COMPLETE
19. [ ] Initial ramdisk (initrd/initramfs)
20. [ ] Filesystem driver (ext2, FAT32, or custom)

## In Progress
- None

## Next Up
- [ ] **Per-process address spaces** - Separate page tables per process
- [ ] **Syscall infrastructure** - SYSCALL/SYSRET entry point, syscall table
- [ ] **User-space entry** - Ring 3 transition (iret/sysret)

## Completed Recently
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
