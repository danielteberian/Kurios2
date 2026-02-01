/* syscall.S - System call wrappers for x86_64 */

.intel_syntax noprefix

.section .note.GNU-stack, "", @progbits

.global syscall0
.global syscall1
.global syscall2
.global syscall3
.global syscall4
.global syscall6

.text

/*
 * x86_64 syscall convention:
 *   RAX = syscall number
 *   RDI = arg1
 *   RSI = arg2
 *   RDX = arg3
 *   R10 = arg4  (not RCX, which is clobbered by SYSCALL)
 *   R8  = arg5
 *   R9  = arg6
 *
 * C calling convention:
 *   RDI = arg1 (syscall num for us)
 *   RSI = arg2 (becomes arg1 for syscall)
 *   RDX = arg3 (becomes arg2 for syscall)
 *   RCX = arg4 (becomes arg3 for syscall)
 *   R8  = arg5 (becomes arg4 for syscall)
 *   R9  = arg6 (becomes arg5 for syscall)
 *   [rsp+8] = arg7 (becomes arg6 for syscall)
 */

/* syscall0(num) */
syscall0:
    mov rax, rdi        /* syscall number */
    syscall
    ret

/* syscall1(num, arg1) */
syscall1:
    mov rax, rdi        /* syscall number */
    mov rdi, rsi        /* arg1 */
    syscall
    ret

/* syscall2(num, arg1, arg2) */
syscall2:
    mov rax, rdi        /* syscall number */
    mov rdi, rsi        /* arg1 */
    mov rsi, rdx        /* arg2 */
    syscall
    ret

/* syscall3(num, arg1, arg2, arg3) */
syscall3:
    mov rax, rdi        /* syscall number */
    mov rdi, rsi        /* arg1 */
    mov rsi, rdx        /* arg2 */
    mov rdx, rcx        /* arg3 */
    syscall
    ret

/* syscall4(num, arg1, arg2, arg3, arg4) */
syscall4:
    mov rax, rdi        /* syscall number */
    mov rdi, rsi        /* arg1 */
    mov rsi, rdx        /* arg2 */
    mov rdx, rcx        /* arg3 */
    mov r10, r8         /* arg4 */
    syscall
    ret

/* syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6) */
syscall6:
    mov rax, rdi        /* syscall number */
    mov rdi, rsi        /* arg1 */
    mov rsi, rdx        /* arg2 */
    mov rdx, rcx        /* arg3 */
    mov r10, r8         /* arg4 */
    mov r8, r9          /* arg5 */
    mov r9, [rsp+8]     /* arg6 from stack */
    syscall
    ret
