/* shm.h - POSIX Shared Memory */
#ifndef _IPC_SHM_H
#define _IPC_SHM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../sync/spinlock.h"
#include "../include/types.h"

/* Shared memory object */
typedef struct shm_object {
    char name[256];             /* Name of shared memory object */
    size_t size;                /* Size of shared memory */
    uint64_t phys_addr;         /* Physical address of shared memory */
    uint64_t virt_addr;         /* Kernel virtual address */
    int refcount;               /* Reference count */
    int fd;                     /* File descriptor (for VFS integration) */
    bool unlinked;              /* Marked for deletion */
    spinlock_t lock;
} shm_object_t;

/* Shared memory functions */
void shm_init(void);
shm_object_t *shm_open_internal(const char *name, int oflag, mode_t mode);
int shm_unlink_internal(const char *name);
void shm_ref(shm_object_t *shm);
void shm_unref(shm_object_t *shm);
shm_object_t *shm_find_by_name(const char *name);

/* Syscall helpers */
int sys_shm_open(const char *name, int oflag, mode_t mode);
int sys_shm_unlink(const char *name);

#endif /* _IPC_SHM_H */
