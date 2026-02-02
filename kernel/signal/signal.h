/* signal.h - Signal Handling */
#ifndef _SIGNAL_SIGNAL_H
#define _SIGNAL_SIGNAL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Standard POSIX signals
 */
#define SIGHUP      1       /* Hangup */
#define SIGINT      2       /* Interrupt (Ctrl+C) */
#define SIGQUIT     3       /* Quit (Ctrl+\) */
#define SIGILL      4       /* Illegal instruction */
#define SIGTRAP     5       /* Trace/breakpoint trap */
#define SIGABRT     6       /* Abort */
#define SIGBUS      7       /* Bus error */
#define SIGFPE      8       /* Floating point exception */
#define SIGKILL     9       /* Kill (cannot be caught or ignored) */
#define SIGUSR1     10      /* User-defined signal 1 */
#define SIGSEGV     11      /* Segmentation fault */
#define SIGUSR2     12      /* User-defined signal 2 */
#define SIGPIPE     13      /* Broken pipe */
#define SIGALRM     14      /* Alarm clock */
#define SIGTERM     15      /* Termination */
#define SIGSTKFLT   16      /* Stack fault */
#define SIGCHLD     17      /* Child status changed */
#define SIGCONT     18      /* Continue */
#define SIGSTOP     19      /* Stop (cannot be caught or ignored) */
#define SIGTSTP     20      /* Terminal stop (Ctrl+Z) */
#define SIGTTIN     21      /* Background read from terminal */
#define SIGTTOU     22      /* Background write to terminal */
#define SIGURG      23      /* Urgent condition on socket */
#define SIGXCPU     24      /* CPU time limit exceeded */
#define SIGXFSZ     25      /* File size limit exceeded */
#define SIGVTALRM   26      /* Virtual timer alarm */
#define SIGPROF     27      /* Profiling timer alarm */
#define SIGWINCH    28      /* Window size change */
#define SIGIO       29      /* I/O possible */
#define SIGPWR      30      /* Power failure */
#define SIGSYS      31      /* Bad system call */

#define NSIG        32      /* Number of signals */

/*
 * Signal action flags (for sa_flags)
 */
#define SA_NOCLDSTOP    0x00000001  /* Don't send SIGCHLD when children stop */
#define SA_NOCLDWAIT    0x00000002  /* Don't create zombie on child death */
#define SA_SIGINFO      0x00000004  /* Use sa_sigaction instead of sa_handler */
#define SA_ONSTACK      0x08000000  /* Use alternate signal stack */
#define SA_RESTART      0x10000000  /* Restart syscalls after signal */
#define SA_NODEFER      0x40000000  /* Don't block signal during handler */
#define SA_RESETHAND    0x80000000  /* Reset to SIG_DFL on entry to handler */

/*
 * Special signal handlers
 */
#define SIG_DFL     ((sighandler_t)0)   /* Default action */
#define SIG_IGN     ((sighandler_t)1)   /* Ignore signal */
#define SIG_ERR     ((sighandler_t)-1)  /* Error return */

/*
 * Signal set type (bitmask of signals)
 */
typedef uint64_t sigset_t;

/*
 * Signal handler function type
 */
typedef void (*sighandler_t)(int signum);

/*
 * Signal action structure (sigaction)
 */
typedef struct {
    sighandler_t sa_handler;    /* Signal handler or SIG_DFL/SIG_IGN */
    sigset_t sa_mask;           /* Signals to block during handler */
    int sa_flags;               /* Signal action flags */
    void *sa_restorer;          /* Signal trampoline (unused) */
} sigaction_t;

/*
 * Signal information structure (for SA_SIGINFO)
 */
typedef struct {
    int si_signo;               /* Signal number */
    int si_errno;               /* Error number */
    int si_code;                /* Signal code (why signal was sent) */
    uint32_t si_pid;            /* Sending process ID */
    uint32_t si_uid;            /* Sending user ID */
    int si_status;              /* Exit status (for SIGCHLD) */
    void *si_addr;              /* Fault address (for SIGSEGV, SIGBUS, etc.) */
    int si_value;               /* Signal value (for sigqueue) */
} siginfo_t;

/*
 * Signal codes (si_code values)
 */
#define SI_USER     0           /* Sent by kill(), sigsend(), or raise() */
#define SI_KERNEL   0x80        /* Sent by kernel */

/*
 * Default signal actions
 */
typedef enum {
    SIG_ACTION_TERM,    /* Terminate process */
    SIG_ACTION_CORE,    /* Terminate with core dump */
    SIG_ACTION_STOP,    /* Stop process */
    SIG_ACTION_CONT,    /* Continue process */
    SIG_ACTION_IGN      /* Ignore signal */
} sig_default_action_t;

/*
 * Per-process signal state
 */
struct signal_state {
    sigset_t pending;                   /* Pending signals */
    sigset_t blocked;                   /* Blocked signals (signal mask) */
    sigaction_t actions[NSIG];          /* Signal actions */
};
typedef struct signal_state signal_state_t;

/*
 * Signal set manipulation macros
 */
#define sigemptyset(set)        (*(set) = 0)
#define sigfillset(set)         (*(set) = ~(sigset_t)0)
#define sigaddset(set, signo)   (*(set) |= (1UL << ((signo) - 1)))
#define sigdelset(set, signo)   (*(set) &= ~(1UL << ((signo) - 1)))
#define sigismember(set, signo) ((*(set) & (1UL << ((signo) - 1))) != 0)

/*
 * Initialize signal subsystem
 */
void signal_init(void);

/*
 * Initialize signal state for a new process
 */
void signal_state_init(signal_state_t *state);

/*
 * Clone signal state (for fork)
 */
void signal_state_clone(signal_state_t *dst, const signal_state_t *src);

/*
 * Send a signal to a process
 *
 * @param pid     Process ID to signal
 * @param signum  Signal number to send
 * @return 0 on success, negative error on failure
 */
int signal_send(uint32_t pid, int signum);

/*
 * Send a signal to the current process
 */
int signal_raise(int signum);

/*
 * Set signal handler (simplified signal() interface)
 *
 * @param signum  Signal number
 * @param handler New handler (SIG_DFL, SIG_IGN, or function)
 * @return Previous handler, or SIG_ERR on error
 */
sighandler_t signal_set(int signum, sighandler_t handler);

/*
 * Get/set signal action
 *
 * @param signum  Signal number
 * @param act     New action (NULL to query only)
 * @param oldact  Previous action (NULL if not needed)
 * @return 0 on success, negative error on failure
 */
int signal_action(int signum, const sigaction_t *act, sigaction_t *oldact);

/*
 * Get/set signal mask
 *
 * @param how     SIG_BLOCK, SIG_UNBLOCK, or SIG_SETMASK
 * @param set     Signal set to use (NULL to query only)
 * @param oldset  Previous mask (NULL if not needed)
 * @return 0 on success, negative error on failure
 */
#define SIG_BLOCK   0   /* Add signals to mask */
#define SIG_UNBLOCK 1   /* Remove signals from mask */
#define SIG_SETMASK 2   /* Set mask to given value */

int signal_procmask(int how, const sigset_t *set, sigset_t *oldset);

/*
 * Check if there are pending unblocked signals
 */
bool signal_pending(void);

/*
 * Get set of pending signals
 */
sigset_t signal_get_pending(void);

/*
 * Signal frame pushed on user stack when delivering a signal
 * This is what the user-space signal handler sees on entry.
 */
typedef struct signal_frame {
    /* Saved user context (restored by sigreturn) */
    uint64_t r15, r14, r13, r12, rbp, rbx;
    uint64_t r9, r8, r10, rdx, rsi, rdi;
    uint64_t rax;
    uint64_t rcx;           /* User RIP */
    uint64_t r11;           /* User RFLAGS */
    uint64_t rsp;           /* User RSP (original) */

    /* Signal information */
    int signum;             /* Signal number */
    uint32_t _pad;
    sigset_t saved_mask;    /* Blocked signals to restore */

    /* Return address (points to sigreturn trampoline) */
    uint64_t trampoline[2]; /* mov rax, SYS_SIGRETURN; syscall */
} __attribute__((packed)) signal_frame_t;

/*
 * Deliver pending signals (called before returning to user mode)
 * This function may not return if the signal causes termination.
 *
 * @param frame  Pointer to the syscall frame (will be modified if signal delivered)
 * @return true if a signal was delivered (frame modified), false otherwise
 */
bool signal_deliver_pending(void *frame);

/*
 * Get default action for a signal
 */
sig_default_action_t signal_default_action(int signum);

/*
 * Send SIGCHLD to parent process
 * Called when a child process exits or stops
 *
 * @param child_pid  PID of the child that changed state
 * @param parent_pid PID of the parent to notify
 */
void signal_send_sigchld(uint32_t child_pid, uint32_t parent_pid);

/*
 * Send SIGCHLD to parent when child stops
 *
 * @param child_pid  PID of the child that stopped
 * @param parent_pid PID of the parent to notify
 */
void signal_send_sigchld_stopped(uint32_t child_pid, uint32_t parent_pid);

/*
 * Send SIGCHLD to parent when child continues
 *
 * @param child_pid  PID of the child that continued
 * @param parent_pid PID of the parent to notify
 */
void signal_send_sigchld_continued(uint32_t child_pid, uint32_t parent_pid);

/*
 * Check if signal is valid
 */
static inline bool signal_valid(int signum) {
    return signum > 0 && signum < NSIG;
}

/*
 * Check if signal can be caught or ignored
 */
static inline bool signal_catchable(int signum) {
    return signum != SIGKILL && signum != SIGSTOP;
}

#endif /* _SIGNAL_SIGNAL_H */
