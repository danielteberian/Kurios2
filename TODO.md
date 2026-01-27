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
13. [ ] Multithreading (kernel threads, scheduler, context switching)
14. [ ] Process abstraction (address spaces, process control blocks)

### Device Drivers
15. [x] Keyboard driver (PS/2 keyboard via IRQ1)
16. [ ] Additional input devices (mouse, etc.)

### Filesystem
17. [ ] VFS layer (virtual filesystem abstraction)
18. [ ] Initial ramdisk (initrd/initramfs)
19. [ ] Filesystem driver (ext2, FAT32, or custom)

## In Progress
(Paused - ready to resume)

## Next Up
- [ ] **ACPI table parsing** - Find MADT, HPET, FADT

## Completed Recently
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
