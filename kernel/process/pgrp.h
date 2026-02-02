/* pgrp.h - Process Groups and Sessions */
#ifndef _PROCESS_PGRP_H
#define _PROCESS_PGRP_H

#include <stdint.h>
#include <stdbool.h>
#include "process.h"

/*
 * Send a signal to all processes in a process group
 *
 * @param pgrp   Process group ID
 * @param sig    Signal number to send
 * @return Number of processes signaled, or negative error code
 */
int pgrp_send_signal(pid_t pgrp, int sig);

/*
 * Check if a process group exists
 *
 * @param pgrp   Process group ID to check
 * @return true if group has at least one member, false otherwise
 */
bool pgrp_exists(pid_t pgrp);

/*
 * Validate setpgid operation according to POSIX rules
 *
 * @param target_pid  PID of process to change group
 * @param new_pgrp    New process group ID
 * @return 0 if valid, negative error code otherwise
 */
int pgrp_validate_setpgid(pid_t target_pid, pid_t new_pgrp);

/*
 * Check if a process group is orphaned
 * An orphaned group has no members with parents in a different group
 * but the same session.
 *
 * @param pgrp   Process group ID to check
 * @return true if group is orphaned, false otherwise
 */
bool pgrp_is_orphaned(pid_t pgrp);

#endif /* _PROCESS_PGRP_H */
