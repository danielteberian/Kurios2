; context.asm - Context switch implementation
; void context_switch(uint64_t *old_rsp, uint64_t new_rsp)
;   rdi = pointer to save current RSP
;   rsi = new RSP to load

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
bits 64

global context_switch
context_switch:
    ; Save callee-saved registers on current stack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    pushfq              ; Save flags

    ; Save current stack pointer
    mov [rdi], rsp

    ; Load new stack pointer
    mov rsp, rsi

    ; Restore callee-saved registers from new stack
    popfq               ; Restore flags
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret                 ; Return to new thread


; Initial thread entry wrapper
; Called when a new thread starts for the first time
; Stack has: entry function, arg, thread_exit address
global thread_entry_trampoline
thread_entry_trampoline:
    ; Enable interrupts for the new thread
    sti

    ; Pop entry function and arg
    pop rdi             ; arg (first parameter)
    pop rax             ; entry function

    ; Call the thread's entry function
    call rax

    ; If entry function returns, call thread_exit
    extern thread_exit
    call thread_exit

    ; Should never reach here
    cli
.halt:
    hlt
    jmp .halt
