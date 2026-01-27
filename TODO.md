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
7. [ ] GDT with TSS (kernel/user segments, per-CPU TSS) **<-- NEXT**
8. [ ] IDT + exception handlers (page fault, GPF, double fault, etc.)

### Hardware Abstraction
8. [ ] ACPI table parsing (find MADT, HPET, FADT)
9. [ ] APIC setup (Local APIC + I/O APIC, replace legacy PIC)
10. [ ] Timer (APIC timer or HPET, PIT as fallback)
11. [ ] Basic spinlocks/mutexes

## In Progress
(Paused - ready to resume)

## Next Up
- [ ] **GDT with TSS** - Proper segment descriptors for kernel/user mode
  - Kernel code/data segments
  - User code/data segments
  - Task State Segment (TSS) for interrupt stack switching

## Completed Recently
- [x] Slab Allocator (kmalloc/kfree) - VERIFIED WORKING (14 tests)
- [x] VMM (Virtual Memory Manager) - VERIFIED WORKING
- [x] PMM (Buddy Allocator) - VERIFIED WORKING
- [x] Stack smash protector - VERIFIED WORKING
- [x] Global constructors/destructors - VERIFIED WORKING
- [x] Debug framework + Higher-half kernel - VERIFIED WORKING
