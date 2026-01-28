/* ramfs.h - RAM filesystem */
#ifndef _KERNEL_RAMFS_H
#define _KERNEL_RAMFS_H

#include "vfs.h"

/* Block size for file data */
#define RAMFS_BLOCK_SIZE    4096
#define RAMFS_INITIAL_BLOCKS 4

/* Initialize and register ramfs with VFS */
void ramfs_init(void);

#endif /* _KERNEL_RAMFS_H */
