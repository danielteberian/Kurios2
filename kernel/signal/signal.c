/* signal.c - Signal Handling Implementation */

#include "signal.h"
#include "../include/types.h"
#include "../process/process.h"
#include "../sched/sched.h"
#include "../sched/thread.h"
#include "../debug/debug.h"
#include "../lib/string.h"
#include "../mm/as.h"

/*
 * Default actions for signals
 */
static const sig_default_action_t default_actions[NSIG] = {
    [0]         = SIG_ACTION_IGN,       /* Signal 0 (null signal) */
    [SIGHUP]    = SIG_ACTION_TERM,
    [SIGINT]    = SIG_ACTION_TERM,
    [SIGQUIT]   = SIG_ACTION_CORE,
    [SIGILL]    = SIG_ACTION_CORE,
    [SIGTRAP]   = SIG_ACTION_CORE,
    [SIGABRT]   = SIG_ACTION_CORE,
    [SIGBUS]    = SIG_ACTION_CORE,
    [SIGFPE]    = SIG_ACTION_CORE,
    [SIGKILL]   = SIG_ACTION_TERM,
    [SIGUSR1]   = SIG_ACTION_TERM,
    [SIGSEGV]   = SIG_ACTION_CORE,
    [SIGUSR2]   = SIG_ACTION_TERM,
    [SIGPIPE]   = SIG_ACTION_TERM,
    [SIGALRM]   = SIG_ACTION_TERM,
    [SIGTERM]   = SIG_ACTION_TERM,
    [SIGSTKFLT] = SIG_ACTION_TERM,
    [SIGCHLD]   = SIG_ACTION_IGN,
    [SIGCONT]   = SIG_ACTION_CONT,
    [SIGSTOP]   = SIG_ACTION_STOP,
    [SIGTSTP]   = SIG_ACTION_STOP,
    [SIGTTIN]   = SIG_ACTION_STOP,
    [SIGTTOU]   = SIG_ACTION_STOP,
    [SIGURG]    = SIG_ACTION_IGN,
    [SIGXCPU]   = SIG_ACTION_CORE,
    [SIGXFSZ]   = SIG_ACTION_CORE,
    [SIGVTALRM] = SIG_ACTION_TERM,
    [SIGPROF]   = SIG_ACTION_TERM,
    [SIGWINCH]  = SIG_ACTION_IGN,
    [SIGIO]     = SIG_ACTION_TERM,
    [SIGPWR]    = SIG_ACTION_TERM,
    [SIGSYS]    = SIG_ACTION_CORE,
};

/*
 * Initialize signal subsystem
 */
void signal_init(void)
{
    INFO("Signal: Subsystem initialized");
}

/*
 * Initialize signal state for a new process
 */
void signal_state_init(signal_state_t *state)
{
    if (!state) {
        return;
    }

    /* Clear pending and blocked signals */
    state->pending = 0;
    state->blocked = 0;

    /* Initialize all handlers to SIG_DFL */
    for (int i = 0; i < NSIG; i++) {
        state->actions[i].sa_handler = SIG_DFL;
        state->actions[i].sa_mask = 0;
        state->actions[i].sa_flags = 0;
        state->actions[i].sa_restorer = NULL;
    }
}

/*
 * Clone signal state (for fork)
 */
void signal_state_clone(signal_state_t *dst, const signal_state_t *src)
{
    if (!dst || !src) {
        return;
    }

    /* Copy pending signals */
    dst->pending = src->pending;

    /* Blocked signals are inherited */
    dst->blocked = src->blocked;

    /* Copy signal actions */
    for (int i = 0; i < NSIG; i++) {
        dst->actions[i] = src->actions[i];
    }
}

/*
 * Get signal state for current process
 */
static signal_state_t *get_current_signal_state(void)
{
    process_t *proc = process_current();
    if (proc && proc->signals) {
        return proc->signals;
    }
    return NULL;
}

/*
 * Send a signal to a process
 */
int signal_send(uint32_t pid, int signum)
{
    /* Signal 0 is used to check if process exists */
    if (signum == 0) {
        process_t *proc = process_get_by_pid(pid);
        return proc ? 0 : -ESRCH;
    }

    /* Validate signal number */
    if (!signal_valid(signum)) {
        return -EINVAL;
    }

    /* Find target process */
    process_t *proc = process_get_by_pid(pid);
    if (!proc) {
        return -ESRCH;
    }

    /* Check if process can receive signals */
    if (proc->state == PROC_ZOMBIE || proc->state == PROC_DEAD) {
        return -ESRCH;
    }

    /* Add signal to pending set */
    if (proc->signals) {
        sigaddset(&proc->signals->pending, signum);
        DEBUG("Signal: Sent signal %d to PID %u", signum, pid);
    }

    /* If signal wakes process, make it ready */
    if (signum == SIGCONT && proc->state == PROC_BLOCKED) {
        /* TODO: Wake up blocked process */
    }

    return 0;
}

/*
 * Send a signal to the current process
 */
int signal_raise(int signum)
{
    process_t *proc = process_current();
    if (!proc) {
        return -ESRCH;
    }
    return signal_send(proc->pid, signum);
}

/*
 * Set signal handler (simplified signal() interface)
 */
sighandler_t signal_set(int signum, sighandler_t handler)
{
    if (!signal_valid(signum)) {
        return SIG_ERR;
    }

    if (!signal_catchable(signum) && handler != SIG_DFL) {
        return SIG_ERR;
    }

    signal_state_t *state = get_current_signal_state();
    if (!state) {
        return SIG_ERR;
    }

    sighandler_t old = state->actions[signum].sa_handler;
    state->actions[signum].sa_handler = handler;
    state->actions[signum].sa_flags = 0;
    sigemptyset(&state->actions[signum].sa_mask);

    return old;
}

/*
 * Get/set signal action
 */
int signal_action(int signum, const sigaction_t *act, sigaction_t *oldact)
{
    if (!signal_valid(signum)) {
        return -EINVAL;
    }

    signal_state_t *state = get_current_signal_state();
    if (!state) {
        return -ESRCH;
    }

    /* Save old action if requested */
    if (oldact) {
        *oldact = state->actions[signum];
    }

    /* Set new action if provided */
    if (act) {
        if (!signal_catchable(signum) && act->sa_handler != SIG_DFL) {
            return -EINVAL;  /* Can't change SIGKILL/SIGSTOP */
        }
        state->actions[signum] = *act;
    }

    return 0;
}

/*
 * Get/set signal mask
 */
int signal_procmask(int how, const sigset_t *set, sigset_t *oldset)
{
    signal_state_t *state = get_current_signal_state();
    if (!state) {
        return -ESRCH;
    }

    /* Save old mask if requested */
    if (oldset) {
        *oldset = state->blocked;
    }

    /* Modify mask if set provided */
    if (set) {
        switch (how) {
        case SIG_BLOCK:
            state->blocked |= *set;
            break;
        case SIG_UNBLOCK:
            state->blocked &= ~(*set);
            break;
        case SIG_SETMASK:
            state->blocked = *set;
            break;
        default:
            return -EINVAL;
        }

        /* Can never block SIGKILL or SIGSTOP */
        sigdelset(&state->blocked, SIGKILL);
        sigdelset(&state->blocked, SIGSTOP);
    }

    return 0;
}

/*
 * Check if there are pending unblocked signals
 */
bool signal_pending(void)
{
    signal_state_t *state = get_current_signal_state();
    if (!state) {
        return false;
    }

    /* Check for any pending signal that isn't blocked */
    sigset_t deliverable = state->pending & ~state->blocked;
    return deliverable != 0;
}

/*
 * Get set of pending signals
 */
sigset_t signal_get_pending(void)
{
    signal_state_t *state = get_current_signal_state();
    if (!state) {
        return 0;
    }
    return state->pending;
}

/*
 * Get default action for a signal
 */
sig_default_action_t signal_default_action(int signum)
{
    if (!signal_valid(signum)) {
        return SIG_ACTION_IGN;
    }
    return default_actions[signum];
}

/*
 * Handle a signal with default action
 */
static void handle_default_signal(process_t *proc, int signum)
{
    sig_default_action_t action = signal_default_action(signum);

    switch (action) {
    case SIG_ACTION_TERM:
    case SIG_ACTION_CORE:
        /* Terminate the process */
        INFO("Signal: Process %u terminated by signal %d", proc->pid, signum);
        process_exit(proc, 128 + signum);  /* Convention: exit code = 128 + signal */
        break;

    case SIG_ACTION_STOP:
        /* Stop the process */
        DEBUG("Signal: Process %u stopped by signal %d", proc->pid, signum);
        proc->state = PROC_BLOCKED;
        /* TODO: Notify parent via SIGCHLD */
        break;

    case SIG_ACTION_CONT:
        /* Continue the process */
        if (proc->state == PROC_BLOCKED) {
            DEBUG("Signal: Process %u continued by signal %d", proc->pid, signum);
            proc->state = PROC_READY;
        }
        break;

    case SIG_ACTION_IGN:
        /* Ignore the signal */
        break;
    }
}

/*
 * Deliver pending signals
 * Called before returning to user mode
 *
 * This modifies the syscall frame to redirect execution to the signal handler.
 * When the handler returns, it executes a trampoline that calls sigreturn,
 * which restores the original context.
 *
 * @param frame_ptr  Pointer to syscall_frame_t
 * @return true if signal was delivered (frame modified), false otherwise
 */
bool signal_deliver_pending(void *frame_ptr)
{
    /* Import syscall frame structure */
    typedef struct {
        uint64_t r15, r14, r13, r12, rbp, rbx;
        uint64_t r9, r8, r10, rdx, rsi, rdi;
        uint64_t rax;
        uint64_t rcx;   /* User RIP */
        uint64_t r11;   /* User RFLAGS */
        uint64_t rsp;   /* User RSP */
    } syscall_frame_t;

    syscall_frame_t *frame = (syscall_frame_t *)frame_ptr;

    signal_state_t *state = get_current_signal_state();
    if (!state) {
        return false;
    }

    process_t *proc = process_current();
    if (!proc) {
        return false;
    }

    /* Find a deliverable signal (pending and not blocked) */
    sigset_t deliverable = state->pending & ~state->blocked;
    if (deliverable == 0) {
        return false;  /* No signals to deliver */
    }

    /* Find first pending signal (lower numbers have priority) */
    int signum = 0;
    for (int i = 1; i < NSIG; i++) {
        if (sigismember(&deliverable, i)) {
            signum = i;
            break;
        }
    }

    if (signum == 0) {
        return false;  /* No signal found */
    }

    /* Clear the signal from pending */
    sigdelset(&state->pending, signum);

    /* Get the action for this signal */
    sigaction_t *action = &state->actions[signum];
    sighandler_t handler = action->sa_handler;

    if (handler == SIG_IGN) {
        /* Signal is ignored - nothing to do */
        return false;
    }

    if (handler == SIG_DFL) {
        /* Default action */
        handle_default_signal(proc, signum);
        /* If process was terminated, we won't return here */
        return false;
    }

    /*
     * Deliver signal to user-space handler:
     * 1. Push signal frame on user stack (saved context + trampoline)
     * 2. Set RDI = signal number (first argument to handler)
     * 3. Set RIP = handler address
     * 4. When handler does "ret", it returns to trampoline
     * 5. Trampoline calls sigreturn syscall to restore context
     */

    DEBUG("Signal: Delivering signal %d to PID %u, handler=0x%llx",
          signum, proc->pid, (unsigned long long)handler);

    /* Calculate where to put signal frame on user stack */
    /* Align to 16 bytes and leave 128-byte red zone */
    uint64_t user_rsp = frame->rsp;
    user_rsp -= 128;                         /* Red zone */
    user_rsp -= sizeof(signal_frame_t);
    user_rsp &= ~0xFUL;                      /* 16-byte alignment */

    /* Build the signal frame in kernel memory first */
    signal_frame_t sig_frame;

    /* Save current register state */
    sig_frame.r15 = frame->r15;
    sig_frame.r14 = frame->r14;
    sig_frame.r13 = frame->r13;
    sig_frame.r12 = frame->r12;
    sig_frame.rbp = frame->rbp;
    sig_frame.rbx = frame->rbx;
    sig_frame.r9 = frame->r9;
    sig_frame.r8 = frame->r8;
    sig_frame.r10 = frame->r10;
    sig_frame.rdx = frame->rdx;
    sig_frame.rsi = frame->rsi;
    sig_frame.rdi = frame->rdi;
    sig_frame.rax = frame->rax;
    sig_frame.rcx = frame->rcx;     /* Original RIP */
    sig_frame.r11 = frame->r11;     /* Original RFLAGS */
    sig_frame.rsp = frame->rsp;     /* Original RSP */

    /* Signal info */
    sig_frame.signum = signum;
    sig_frame._pad = 0;
    sig_frame.saved_mask = state->blocked;

    /*
     * Trampoline code:
     *   mov rax, 15          ; SYS_SIGRETURN = 15
     *   syscall
     *
     * Encoded as:
     *   48 c7 c0 0f 00 00 00   mov rax, 0xf
     *   0f 05                  syscall
     */
    sig_frame.trampoline[0] = 0x0000000fc0c74800UL | (15UL << 32);  /* mov rax, 15 */
    sig_frame.trampoline[1] = 0x050f;                               /* syscall */

    /* Copy signal frame to user stack */
    /* Note: In a real kernel, we'd use copy_to_user with proper checks */
    signal_frame_t *user_frame = (signal_frame_t *)user_rsp;

    /* Check that user stack is accessible */
    if (user_rsp < 0x1000 || user_rsp >= 0x00007FFFFFFFFFFF) {
        WARN("Signal: User stack overflow, terminating process");
        handle_default_signal(proc, SIGSEGV);
        return false;
    }

    /* Copy frame to user stack (assuming user pages are mapped) */
    memcpy(user_frame, &sig_frame, sizeof(signal_frame_t));

    /* Apply signal mask for handler execution */
    state->blocked |= action->sa_mask;
    if (!(action->sa_flags & SA_NODEFER)) {
        sigaddset(&state->blocked, signum);  /* Block signal during handler */
    }

    /* Reset handler if SA_RESETHAND */
    if (action->sa_flags & SA_RESETHAND) {
        action->sa_handler = SIG_DFL;
    }

    /* Modify syscall frame to redirect to signal handler */
    frame->rdi = (uint64_t)signum;                           /* First arg = signal number */
    frame->rcx = (uint64_t)handler;                          /* RIP = handler address */
    frame->rsp = user_rsp + sizeof(signal_frame_t) - 16;     /* RSP = just below trampoline */

    /* Set return address to trampoline (handler will "ret" to it) */
    uint64_t *ret_addr = (uint64_t *)(user_rsp + sizeof(signal_frame_t) - 16);
    *ret_addr = user_rsp + offsetof(signal_frame_t, trampoline);

    DEBUG("Signal: Frame at 0x%llx, handler 0x%llx, return to trampoline 0x%llx",
          (unsigned long long)user_rsp,
          (unsigned long long)handler,
          (unsigned long long)(user_rsp + offsetof(signal_frame_t, trampoline)));

    return true;
}

/*
 * Send SIGCHLD to parent process when child exits/stops
 */
void signal_send_sigchld(uint32_t child_pid, uint32_t parent_pid)
{
    process_t *parent = process_get_by_pid(parent_pid);
    if (!parent || !parent->signals) {
        return;
    }

    /* Check if parent wants SIGCHLD */
    sigaction_t *action = &parent->signals->actions[SIGCHLD];

    /* SA_NOCLDSTOP: don't notify for stopped children (we only call this on exit for now) */
    /* SA_NOCLDWAIT: auto-reap children, but still send signal */

    if (action->sa_handler != SIG_IGN) {
        /* Add SIGCHLD to pending signals */
        sigaddset(&parent->signals->pending, SIGCHLD);
        DEBUG("Signal: Sent SIGCHLD to parent PID %u (child %u exited)", parent_pid, child_pid);
    }

    /* If parent is blocked waiting, we might want to wake it up */
    /* For now, the scheduler will handle this on next reschedule */
}

#ifdef DEBUG_TESTS
/*
 * Run signal subsystem tests
 */
void signal_run_tests(void)
{
    kprintf("\n=== Signal Tests ===\n");

    /* Test 1: Signal set operations */
    sigset_t set;
    sigemptyset(&set);
    kprintf("  Test 1 - Empty set: %s\n", (set == 0) ? "OK" : "FAIL");

    sigaddset(&set, SIGINT);
    kprintf("  Test 2 - Add SIGINT: %s\n",
            sigismember(&set, SIGINT) ? "OK" : "FAIL");

    sigaddset(&set, SIGTERM);
    kprintf("  Test 3 - Add SIGTERM: %s\n",
            (sigismember(&set, SIGINT) && sigismember(&set, SIGTERM)) ? "OK" : "FAIL");

    sigdelset(&set, SIGINT);
    kprintf("  Test 4 - Del SIGINT: %s\n",
            (!sigismember(&set, SIGINT) && sigismember(&set, SIGTERM)) ? "OK" : "FAIL");

    /* Test 5: Signal validation */
    kprintf("  Test 5 - Valid signals: SIGINT=%s, 0=%s, 32=%s\n",
            signal_valid(SIGINT) ? "yes" : "no",
            signal_valid(0) ? "yes" : "no",
            signal_valid(32) ? "yes" : "no");

    /* Test 6: Catchable signals */
    kprintf("  Test 6 - Catchable: SIGINT=%s, SIGKILL=%s, SIGSTOP=%s\n",
            signal_catchable(SIGINT) ? "yes" : "no",
            signal_catchable(SIGKILL) ? "yes" : "no",
            signal_catchable(SIGSTOP) ? "yes" : "no");

    /* Test 7: Default actions */
    kprintf("  Test 7 - Default actions:\n");
    kprintf("    SIGINT=%d (TERM), SIGSEGV=%d (CORE), SIGCHLD=%d (IGN)\n",
            signal_default_action(SIGINT),
            signal_default_action(SIGSEGV),
            signal_default_action(SIGCHLD));

    /* Test 8: Signal state init */
    signal_state_t test_state;
    signal_state_init(&test_state);
    kprintf("  Test 8 - State init: pending=%llu, blocked=%llu %s\n",
            (unsigned long long)test_state.pending,
            (unsigned long long)test_state.blocked,
            (test_state.pending == 0 && test_state.blocked == 0) ? "OK" : "FAIL");

    kprintf("\n  Signal tests complete.\n\n");
}
#endif /* DEBUG_TESTS */
