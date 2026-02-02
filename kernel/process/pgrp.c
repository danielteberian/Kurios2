/* pgrp.c - Process Groups and Sessions Implementation */

#include "pgrp.h"
#include "process.h"
#include "../include/types.h"
#include "../signal/signal.h"
#include "../debug/debug.h"

/*
 * Send a signal to all processes in a process group
 */
int pgrp_send_signal(pid_t pgrp, int sig)
{
    int count = 0;
    int err = -ESRCH;  /* No such process group */

    /* Iterate through all processes */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *proc = process_get_by_pid(i);
        if (!proc) {
            continue;
        }

        /* Check if process is in target group */
        if (proc->pgrp != pgrp) {
            continue;
        }

        /* Skip zombie and dead processes */
        if (proc->state == PROC_ZOMBIE || proc->state == PROC_DEAD) {
            continue;
        }

        /* Send signal to this process */
        int ret = signal_send(i, sig);
        if (ret == 0) {
            count++;
            err = 0;  /* At least one signal succeeded */
        }
    }

    return count > 0 ? count : err;
}

/*
 * Check if a process group exists
 */
bool pgrp_exists(pid_t pgrp)
{
    /* Iterate through all processes */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *proc = process_get_by_pid(i);
        if (!proc) {
            continue;
        }

        /* Check if process is in target group */
        if (proc->pgrp == pgrp) {
            /* Skip zombie and dead processes */
            if (proc->state != PROC_ZOMBIE && proc->state != PROC_DEAD) {
                return true;
            }
        }
    }

    return false;
}

/*
 * Validate setpgid operation according to POSIX rules
 */
int pgrp_validate_setpgid(pid_t target_pid, pid_t new_pgrp)
{
    process_t *current = process_current();
    if (!current) {
        return -ESRCH;
    }

    /* Can't set pgid for PID 0 (kernel process) */
    if (target_pid == 0) {
        return -EINVAL;
    }

    /* Get target process */
    process_t *target = process_get_by_pid(target_pid);
    if (!target) {
        return -ESRCH;
    }

    /* Can only call setpgid on self or child */
    if (target_pid != current->pid && target->parent_pid != current->pid) {
        return -ESRCH;
    }

    /* Can't change pgid of child after it has exec'd */
    if (target_pid != current->pid && target->exec_count > 0) {
        return -EACCES;
    }

    /* If new_pgrp is 0, use target's PID */
    if (new_pgrp == 0) {
        new_pgrp = target_pid;
    }

    /* Can't move to different session */
    if (new_pgrp != target_pid) {
        /* Find a process with the target pgrp */
        bool found_group = false;
        for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
            process_t *proc = process_get_by_pid(i);
            if (proc && proc->pgrp == new_pgrp &&
                proc->state != PROC_ZOMBIE && proc->state != PROC_DEAD) {
                /* Check same session */
                if (proc->session_id != target->session_id) {
                    return -EPERM;
                }
                found_group = true;
                break;
            }
        }

        /* If group doesn't exist, this creates a new one in the same session */
        if (!found_group) {
            /* New group ID must match a PID in the same session */
            process_t *leader = process_get_by_pid(new_pgrp);
            if (!leader || leader->session_id != target->session_id) {
                return -EPERM;
            }
        }
    }

    return 0;  /* Valid */
}

/*
 * Check if a process group is orphaned
 */
bool pgrp_is_orphaned(pid_t pgrp)
{
    pid_t session_id = 0;
    bool found_any = false;

    /* First pass: find session ID and check for any members */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *proc = process_get_by_pid(i);
        if (!proc || proc->pgrp != pgrp) {
            continue;
        }

        if (proc->state == PROC_ZOMBIE || proc->state == PROC_DEAD) {
            continue;
        }

        session_id = proc->session_id;
        found_any = true;
        break;
    }

    if (!found_any) {
        return true;  /* No members = orphaned */
    }

    /* Second pass: check if any member has a parent in same session but different pgrp */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *proc = process_get_by_pid(i);
        if (!proc || proc->pgrp != pgrp) {
            continue;
        }

        if (proc->state == PROC_ZOMBIE || proc->state == PROC_DEAD) {
            continue;
        }

        /* Check parent */
        process_t *parent = process_get_by_pid(proc->parent_pid);
        if (parent &&
            parent->session_id == session_id &&
            parent->pgrp != pgrp &&
            parent->state != PROC_ZOMBIE &&
            parent->state != PROC_DEAD) {
            /* Has a parent in same session but different group */
            return false;  /* Not orphaned */
        }
    }

    return true;  /* Orphaned: no members have parents in same session */
}
