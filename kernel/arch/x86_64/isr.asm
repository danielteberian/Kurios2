; isr.asm - Interrupt Service Routine stubs
; Handles saving CPU state and calling C handlers

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
bits 64

; External C handlers
extern exception_handler
extern irq_handler

;------------------------------------------------------------------------------
; Common ISR stub - saves all registers and calls the C handler
;------------------------------------------------------------------------------
%macro ISR_COMMON 1
    ; At this point the stack has:
    ; [rsp+40] SS
    ; [rsp+32] RSP
    ; [rsp+24] RFLAGS
    ; [rsp+16] CS
    ; [rsp+8]  RIP
    ; [rsp+0]  Error code (or dummy)
    ; Plus we pushed the interrupt number

    ; Save all general-purpose registers
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Pass pointer to cpu_state_t as first argument
    mov rdi, rsp

    ; Call the C handler
    call %1

    ; Restore all general-purpose registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax

    ; Remove error code and interrupt number from stack
    add rsp, 16

    ; Return from interrupt
    iretq
%endmacro

;------------------------------------------------------------------------------
; ISR stub for exceptions WITHOUT error code
; Pushes a dummy error code (0) for consistent stack layout
;------------------------------------------------------------------------------
%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0        ; Dummy error code
    push qword %1       ; Interrupt number
    ISR_COMMON exception_handler
%endmacro

;------------------------------------------------------------------------------
; ISR stub for exceptions WITH error code
; CPU already pushed error code
;------------------------------------------------------------------------------
%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1       ; Interrupt number
    ISR_COMMON exception_handler
%endmacro

;------------------------------------------------------------------------------
; IRQ stub
;------------------------------------------------------------------------------
%macro IRQ 2
global irq%1
irq%1:
    push qword 0        ; Dummy error code
    push qword %2       ; Interrupt number (IRQ + 32)
    ISR_COMMON irq_handler
%endmacro

;------------------------------------------------------------------------------
; Exception handlers (0-31)
;
; Exceptions with error codes: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30
; All others push dummy error code
;------------------------------------------------------------------------------
ISR_NOERR 0     ; #DE - Divide Error
ISR_NOERR 1     ; #DB - Debug
ISR_NOERR 2     ; NMI
ISR_NOERR 3     ; #BP - Breakpoint
ISR_NOERR 4     ; #OF - Overflow
ISR_NOERR 5     ; #BR - Bound Range Exceeded
ISR_NOERR 6     ; #UD - Invalid Opcode
ISR_NOERR 7     ; #NM - Device Not Available
ISR_ERR   8     ; #DF - Double Fault
ISR_NOERR 9     ; Coprocessor Segment Overrun (reserved)
ISR_ERR   10    ; #TS - Invalid TSS
ISR_ERR   11    ; #NP - Segment Not Present
ISR_ERR   12    ; #SS - Stack-Segment Fault
ISR_ERR   13    ; #GP - General Protection Fault
ISR_ERR   14    ; #PF - Page Fault
ISR_NOERR 15    ; Reserved
ISR_NOERR 16    ; #MF - x87 FPU Error
ISR_ERR   17    ; #AC - Alignment Check
ISR_NOERR 18    ; #MC - Machine Check
ISR_NOERR 19    ; #XM - SIMD Floating-Point
ISR_NOERR 20    ; #VE - Virtualization Exception
ISR_ERR   21    ; #CP - Control Protection Exception
ISR_NOERR 22    ; Reserved
ISR_NOERR 23    ; Reserved
ISR_NOERR 24    ; Reserved
ISR_NOERR 25    ; Reserved
ISR_NOERR 26    ; Reserved
ISR_NOERR 27    ; Reserved
ISR_NOERR 28    ; Reserved
ISR_ERR   29    ; #SX - Security Exception (VMX)
ISR_ERR   30    ; Reserved (hypervisor injection)
ISR_NOERR 31    ; Reserved

;------------------------------------------------------------------------------
; IRQ handlers (remapped to INT 32-47)
;------------------------------------------------------------------------------
IRQ 0,  32      ; Timer
IRQ 1,  33      ; Keyboard
IRQ 2,  34      ; Cascade
IRQ 3,  35      ; COM2
IRQ 4,  36      ; COM1
IRQ 5,  37      ; LPT2
IRQ 6,  38      ; Floppy
IRQ 7,  39      ; LPT1 / Spurious
IRQ 8,  40      ; RTC
IRQ 9,  41      ; ACPI
IRQ 10, 42      ; Available
IRQ 11, 43      ; Available
IRQ 12, 44      ; Mouse
IRQ 13, 45      ; FPU
IRQ 14, 46      ; ATA Primary
IRQ 15, 47      ; ATA Secondary

;------------------------------------------------------------------------------
; LAPIC and IPI interrupt handlers (48, 240-242)
; These use the same IRQ macro since they're external interrupts
;------------------------------------------------------------------------------
IRQ 16, 48      ; LAPIC Timer
IRQ 208, 240    ; IPI Reschedule
IRQ 209, 241    ; IPI TLB Shootdown
IRQ 210, 242    ; IPI Halt

;------------------------------------------------------------------------------
; idt_flush - Load the IDT register
;
; void idt_flush(idt_pointer_t *idt_ptr);
;------------------------------------------------------------------------------
global idt_flush
idt_flush:
    lidt [rdi]
    ret
