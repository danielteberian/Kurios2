; syscall_entry.asm - SYSCALL/SYSRET entry point
;
; When SYSCALL is executed:
;   - RCX = user RIP (return address)
;   - R11 = user RFLAGS
;   - CS and SS are loaded from STAR MSR
;   - Interrupts are disabled (SFMASK clears IF)
;   - We're now in ring 0 but still on user stack!
;
; We must:
;   1. Switch to kernel stack
;   2. Save user state
;   3. Call C syscall dispatcher
;   4. Restore user state
;   5. Return via SYSRET

section .text
bits 64

; External symbols
extern syscall_dispatch

; Export symbols
global syscall_entry
global fork_child_return

;------------------------------------------------------------------------------
; syscall_entry - Entry point for SYSCALL instruction
;
; On entry (from SYSCALL):
;   RCX = user RIP
;   R11 = user RFLAGS
;   RAX = syscall number
;   RDI, RSI, RDX, R10, R8, R9 = syscall arguments
;   RSP = user stack pointer
;
; The SYSCALL instruction has already:
;   - Set CS:SS to kernel segments
;   - Saved user RIP to RCX
;   - Saved user RFLAGS to R11
;   - Cleared RFLAGS bits according to SFMASK (including IF)
;
; We're still on the user stack! Must switch to kernel stack first.
;------------------------------------------------------------------------------
syscall_entry:
    ; Save user RSP to scratch space before switching stacks
    ; We use a fixed location since we don't have per-CPU data yet
    mov [rel user_rsp_scratch], rsp

    ; Switch to kernel syscall stack
    mov rsp, kernel_syscall_stack_top

    ; Now build the syscall frame by pushing registers
    ; Order must match syscall_frame_t (push in reverse order)

    ; Push user RSP (from scratch space)
    push qword [rel user_rsp_scratch]   ; FRAME_RSP (offset 120)

    ; Push registers saved by SYSCALL instruction
    push r11                    ; FRAME_R11 - user RFLAGS (offset 112)
    push rcx                    ; FRAME_RCX - user RIP (offset 104)

    ; Push syscall number
    push rax                    ; FRAME_RAX (offset 96)

    ; Push syscall arguments
    push rdi                    ; FRAME_RDI - arg1 (offset 88)
    push rsi                    ; FRAME_RSI - arg2 (offset 80)
    push rdx                    ; FRAME_RDX - arg3 (offset 72)
    push r10                    ; FRAME_R10 - arg4 (offset 64)
    push r8                     ; FRAME_R8  - arg5 (offset 56)
    push r9                     ; FRAME_R9  - arg6 (offset 48)

    ; Push callee-saved registers (ABI requires we preserve these)
    push rbx                    ; FRAME_RBX (offset 40)
    push rbp                    ; FRAME_RBP (offset 32)
    push r12                    ; FRAME_R12 (offset 24)
    push r13                    ; FRAME_R13 (offset 16)
    push r14                    ; FRAME_R14 (offset 8)
    push r15                    ; FRAME_R15 (offset 0)

    ; Call C dispatcher with pointer to frame
    ; First argument (RDI) = pointer to syscall_frame_t
    mov rdi, rsp
    call syscall_dispatch

    ; Return value is in RAX - keep it there for SYSRET

    ; Restore callee-saved registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; Skip over argument registers in frame (don't need to restore)
    ; r9, r8, r10, rdx, rsi, rdi = 6 * 8 = 48 bytes
    add rsp, 48

    ; Skip over saved syscall number (RAX already has return value)
    add rsp, 8

    ; Restore RCX (user RIP) and R11 (user RFLAGS) for SYSRET
    pop rcx                     ; user RIP
    pop r11                     ; user RFLAGS

    ; Restore user RSP (switches back to user stack)
    pop rsp

    ; Return to user mode via SYSRET
    ;
    ; SYSRET will:
    ;   - Set RIP = RCX
    ;   - Set RFLAGS = (R11 & 0x3C7FD7) | 2  (some bits are forced)
    ;   - Set CS = STAR[63:48] + 16 (user code selector with RPL 3)
    ;   - Set SS = STAR[63:48] + 8 (user data selector with RPL 3)
    ;   - Switch to ring 3
    ;
    ; RAX contains the syscall return value

    o64 sysret                     ; 64-bit return to user mode

;------------------------------------------------------------------------------
; fork_child_return - Return to user mode for fork child
;
; Called with:
;   RDI = pointer to syscall_frame_t
;
; This function never returns - it executes SYSRET to return to user mode.
; The child process calls this to "return" from fork() with RAX=0.
;------------------------------------------------------------------------------
fork_child_return:
    ; RDI points to syscall_frame_t
    ; Load return value (should be 0 for child)
    mov rax, [rdi + 96]         ; frame->rax

    ; Load user RIP and RFLAGS for SYSRET
    mov rcx, [rdi + 104]        ; frame->rcx = user RIP
    mov r11, [rdi + 112]        ; frame->r11 = user RFLAGS

    ; Load user stack pointer
    mov rsp, [rdi + 120]        ; frame->rsp = user RSP

    ; Clear other registers for security (prevent info leaks)
    xor rdi, rdi
    xor rsi, rsi
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor rbx, rbx
    xor rbp, rbp
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15

    ; Return to user mode
    o64 sysret

;------------------------------------------------------------------------------
; Scratch space and kernel syscall stack
;------------------------------------------------------------------------------
section .data

; Scratch space for saving user RSP during stack switch
; In a real OS with per-CPU data, this would be in the GS-based per-CPU area
align 8
user_rsp_scratch:
    dq 0

section .bss
align 4096

; Kernel syscall stack (16 KB)
; Each CPU would have its own stack in a real SMP system
kernel_syscall_stack:
    resb 16384
kernel_syscall_stack_top:

; Mark stack as non-executable (suppresses linker warning)
section .note.GNU-stack noalloc noexec nowrite progbits
