# Kurios2 - 64-bit OS Kernel TODO

## Kernel Foundation (in order)

### Core Foundation
0. [x] Testing and debug framework (serial output, assertions, panic handler)
1. [x] Higher-half kernel + updated linker script
2. [ ] GDT with TSS (kernel/user segments, per-CPU TSS)
3. [ ] IDT + exception handlers (page fault, GPF, double fault, etc.)
4. [ ] Physical memory manager (bitmap allocator, parse boot memory map)
5. [ ] Virtual memory manager (page table management, map/unmap API)
6. [ ] Kernel heap (kmalloc/kfree)
7. [ ] Serial output + kprintf (enhance debug framework)

### Hardware Abstraction
8. [ ] ACPI table parsing (find MADT, HPET, FADT)
9. [ ] APIC setup (Local APIC + I/O APIC, replace legacy PIC)
10. [ ] Timer (APIC timer or HPET, PIT as fallback)
11. [ ] Basic spinlocks/mutexes

## In Progress
(None)

## Blocked / Awaiting Verification
- [x] #0 + #1: Debug framework + Higher-half kernel - **READY FOR TESTING**
  - Test with: `make run-bios`
  - Serial output visible in QEMU terminal (via -serial stdio)
  - Should show debug messages and confirm higher-half execution
