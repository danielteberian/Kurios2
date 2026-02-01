/* sched.h - Scheduler */
#ifndef _SCHED_SCHED_H
#define _SCHED_SCHED_H

#include "thread.h"

/* Forward declaration for SMP */
struct percpu_data;

/* Initialize scheduler (BSP only, during single-CPU boot) */
void sched_init(void);

/* Initialize scheduler for a specific CPU (SMP) */
void sched_init_cpu(struct percpu_data *percpu);

/* Start the scheduler (called once, doesn't return) */
void sched_start(void);

/* Add thread to ready queue */
void sched_ready(thread_t *thread);

/* Remove thread from ready queue */
void sched_remove(thread_t *thread);

/* Pick next thread to run (called by timer) */
void sched_tick(void);

/* Trigger a reschedule */
void sched_reschedule(void);

/* Check if scheduler is running */
bool sched_is_running(void);

/* Context switch to thread (assembly) */
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

#endif /* _SCHED_SCHED_H */
