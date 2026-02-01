/* procfs.h - /proc Filesystem */
#ifndef _FS_PROCFS_H
#define _FS_PROCFS_H

#include "vfs.h"

/*
 * Initialize the proc filesystem
 * This registers procfs with the VFS.
 */
void procfs_init(void);

/*
 * Mount procfs at /proc
 * Should be called after VFS is initialized and root is mounted.
 * @return 0 on success, negative error on failure
 */
int procfs_mount(void);

#ifdef DEBUG_TESTS
/*
 * Run procfs tests
 */
void procfs_run_tests(void);
#endif

#endif /* _FS_PROCFS_H */
