/* syscall.h - System Call Interface */
#ifndef _SYSCALL_SYSCALL_H
#define _SYSCALL_SYSCALL_H

#include <stdint.h>

/*
 * System Call Numbers
 *
 * These are the syscall numbers passed in RAX.
 * Convention follows Linux x86_64 ABI for familiarity.
 */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_FSTAT       5
#define SYS_LSEEK       8
#define SYS_DUP         32
#define SYS_DUP2        33
#define SYS_GETPID      39
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61      /* waitpid - wait for child process */
#define SYS_KILL        62      /* kill - send signal */
#define SYS_GETPPID     110

/* waitpid options */
#define WNOHANG         1       /* Don't block waiting */
#define WUNTRACED       2       /* Also return stopped children */

/* waitpid macros - extract info from status */
#define WIFEXITED(status)       (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)     (((status) >> 8) & 0xff)
#define WIFSIGNALED(status)     (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status)        ((status) & 0x7f)
#define WIFSTOPPED(status)      (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status)        (((status) >> 8) & 0xff)

/* Maximum syscall number */
#define SYS_MAX         256

/*
 * Syscall Register Convention (x86_64)
 *
 * Input:
 *   RAX = syscall number
 *   RDI = arg1
 *   RSI = arg2
 *   RDX = arg3
 *   R10 = arg4 (not RCX, which is clobbered by SYSCALL)
 *   R8  = arg5
 *   R9  = arg6
 *
 * Output:
 *   RAX = return value (negative for error)
 *
 * Preserved by SYSCALL:
 *   RCX = return RIP (saved by CPU)
 *   R11 = return RFLAGS (saved by CPU)
 *
 * The kernel must preserve: RBX, RBP, R12-R15
 */

/*
 * Syscall frame - saved registers on kernel stack during syscall
 */
typedef struct {
    /* Callee-saved registers (we must preserve these) */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;

    /* Syscall arguments (caller-saved, but we save for C handler) */
    uint64_t r9;        /* arg6 */
    uint64_t r8;        /* arg5 */
    uint64_t r10;       /* arg4 */
    uint64_t rdx;       /* arg3 */
    uint64_t rsi;       /* arg2 */
    uint64_t rdi;       /* arg1 */

    /* Syscall number */
    uint64_t rax;

    /* Saved by SYSCALL instruction (we push these) */
    uint64_t rcx;       /* User RIP */
    uint64_t r11;       /* User RFLAGS */

    /* User stack pointer (we save this) */
    uint64_t rsp;       /* User RSP */
} __attribute__((packed)) syscall_frame_t;

/*
 * Syscall handler function type
 * Takes up to 6 arguments, returns int64_t (negative = error)
 */
typedef int64_t (*syscall_handler_t)(uint64_t arg1, uint64_t arg2,
                                     uint64_t arg3, uint64_t arg4,
                                     uint64_t arg5, uint64_t arg6);

/*
 * Initialize syscall infrastructure
 * Configures MSRs for SYSCALL/SYSRET and registers handlers
 */
void syscall_init(void);

/*
 * Register a syscall handler
 *
 * @param num     Syscall number
 * @param handler Handler function
 * @return 0 on success, -1 if invalid number or already registered
 */
int syscall_register(int num, syscall_handler_t handler);

/*
 * Main syscall dispatcher (called from assembly)
 * This function is called with the syscall frame on the stack.
 *
 * @param frame  Pointer to saved registers
 * @return Return value to put in RAX
 */
int64_t syscall_dispatch(syscall_frame_t *frame);

/*
 * Assembly entry point (in syscall_entry.asm)
 */
extern void syscall_entry(void);

#ifdef DEBUG_TESTS
/*
 * Run syscall infrastructure tests
 */
void syscall_run_tests(void);
#endif

#endif /* _SYSCALL_SYSCALL_H */
