; entry.asm - Kernel entry point for higher-half kernel
; Called by bootloader with:
;   - RDI = pointer to boot_info structure (physical address)
;   - Paging enabled with both identity mapping and higher-half mapping
;
; The bootloader maps:
;   - 0x0000000000000000 - 0x0000000100000000 (first 4GB) identity mapped
;   - 0xFFFFFFFF80000000+ mapped to kernel physical at 0x200000

[BITS 64]

; Mark stack as non-executable
section .note.GNU-stack noalloc noexec nowrite progbits

; Constants
KERNEL_VIRT_BASE equ 0xFFFFFFFF80000000

; Exports
global _start

; Imports
extern kernel_main
extern _bss_start
extern _bss_end

; ============================================================================
; Kernel entry point
; The bootloader jumps here (at virtual address 0xFFFFFFFF80000000)
; RDI contains boot_info pointer (physical address, accessible via identity map)
; ============================================================================
section .text
_start:
    ; Save boot_info pointer FIRST (before any EDI modification!)
    ; CRITICAL: In 64-bit mode, writing to EDI zero-extends to RDI, destroying it
    mov r15, rdi

    ; Set up kernel stack (in higher half)
    mov rsp, stack_top

    ; Clear direction flag
    cld

    ; Clear RFLAGS (except reserved bits)
    push 0
    popfq

    ; Zero out BSS section
    ; _bss_start and _bss_end are virtual addresses
    mov rdi, _bss_start
    mov rcx, _bss_end
    sub rcx, rdi
    shr rcx, 3              ; Divide by 8 (qwords)
    xor rax, rax
    rep stosq

    ; Restore boot_info pointer for kernel_main
    mov rdi, r15

    ; Call kernel_main(boot_info)
    call kernel_main

    ; kernel_main should never return, but just in case...
.halt:
    cli
    hlt
    jmp .halt

; ============================================================================
; BSS Section - Stack and other uninitialized data
; ============================================================================
section .bss
align 4096

; Kernel stack (64KB)
stack_bottom:
    resb 65536
stack_top:

; Space for initial page tables (for future use when kernel remaps itself)
align 4096
kernel_pml4:
    resb 4096
kernel_pdpt:
    resb 4096
kernel_pd:
    resb 4096
kernel_pt:
    resb 4096

; Per-CPU data area (for future SMP support)
align 4096
percpu_area:
    resb 4096
