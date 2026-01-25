# Kurios2 - 64-bit OS Kernel TODO

## Kernel Foundation (in order)

### Core Foundation
0. [x] Testing and debug framework (serial output, assertions, panic handler)
1. [x] Higher-half kernel + updated linker script
2. [x] Global constructors/destructors
3. [x] Stack smash protector
4. [x] Physical memory manager (buddy allocator)
5. [x] Virtual memory manager (page table management, map/unmap API)
6. [ ] Kernel heap (slab allocator - kmalloc/kfree) **<-- NEXT**
7. [ ] GDT with TSS (kernel/user segments, per-CPU TSS)
8. [ ] IDT + exception handlers (page fault, GPF, double fault, etc.)

### Hardware Abstraction
8. [ ] ACPI table parsing (find MADT, HPET, FADT)
9. [ ] APIC setup (Local APIC + I/O APIC, replace legacy PIC)
10. [ ] Timer (APIC timer or HPET, PIT as fallback)
11. [ ] Basic spinlocks/mutexes

## In Progress
(Paused - ready to resume)

## Next Up
- [ ] **Kernel heap (Slab Allocator)** - kmalloc/kfree for dynamic allocation
  - Object caches for fixed-size allocations
  - General-purpose kmalloc for variable sizes

## Completed Recently
- [x] VMM (Virtual Memory Manager) - VERIFIED WORKING
- [x] PMM (Buddy Allocator) - VERIFIED WORKING
- [x] Stack smash protector - VERIFIED WORKING
- [x] Global constructors/destructors - VERIFIED WORKING
- [x] Debug framework + Higher-half kernel - VERIFIED WORKING
