; entry.asm - Kernel entry point
; Called by bootloader with boot_info pointer in RDI

[BITS 64]

; Mark stack as non-executable (security)
section .note.GNU-stack noalloc noexec nowrite progbits

global _start
extern kernel_main

section .text

_start:
    ; Set up stack
    mov rsp, stack_top

    ; RDI already contains boot_info pointer from bootloader
    ; Call C kernel main
    call kernel_main

    ; Halt if kernel returns
.halt:
    cli
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384          ; 16KB stack
stack_top:
