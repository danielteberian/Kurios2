; ring3_enter.asm - Enter user mode via IRETQ
;
; The IRETQ instruction pops the following from the stack:
;   1. RIP    - Return instruction pointer
;   2. CS     - Code segment selector (with RPL)
;   3. RFLAGS - Processor flags
;   4. RSP    - Stack pointer
;   5. SS     - Stack segment selector (with RPL)
;
; If CS.RPL != CPL, this causes a privilege level change to ring 3.

section .text
bits 64

global ring3_enter

;------------------------------------------------------------------------------
; ring3_enter - Enter ring 3 (user mode) via IRETQ
;
; Arguments (System V AMD64 ABI):
;   RDI = user RIP
;   RSI = user CS (with RPL 3)
;   RDX = user RFLAGS
;   RCX = user RSP
;   R8  = user SS (with RPL 3)
;
; This function never returns - it performs IRETQ to user mode
;------------------------------------------------------------------------------
ring3_enter:
    ; Build the IRETQ frame on the stack
    ; Push in reverse order: SS, RSP, RFLAGS, CS, RIP

    push r8         ; SS (user data segment with RPL 3)
    push rcx        ; RSP (user stack pointer)
    push rdx        ; RFLAGS
    push rsi        ; CS (user code segment with RPL 3)
    push rdi        ; RIP (user entry point)

    ; Clear all general-purpose registers for security
    ; (don't leak kernel data to user mode)
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    ; Perform the ring transition
    ; IRETQ pops: RIP, CS, RFLAGS, RSP, SS
    ; Because CS.RPL (3) != CPL (0), this transitions to ring 3
    iretq

; Mark stack as non-executable
section .note.GNU-stack noalloc noexec nowrite progbits
