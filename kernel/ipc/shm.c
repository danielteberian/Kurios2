/* shm.c - POSIX Shared Memory Implementation */

#include "shm.h"
#include "../mm/slab.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../lib/string.h"
#include "../debug/debug.h"
#include "../fs/vfs.h"

/* Shared memory table */
#define MAX_SHM_OBJECTS     64
static shm_object_t *shm_objects[MAX_SHM_OBJECTS];
static spinlock_t shm_table_lock = SPINLOCK_INIT;

/* Virtual address range for shared memory mappings in kernel */
#define SHM_VIRT_BASE       0xFFFFFFFF92000000UL
#define SHM_VIRT_SIZE       0x10000000UL  /* 256 MB */
static uint64_t shm_virt_next = SHM_VIRT_BASE;

/*
 * Initialize shared memory subsystem
 */
void shm_init(void) {
    memset(shm_objects, 0, sizeof(shm_objects));
    spin_init(&shm_table_lock);
    INFO("Shared memory subsystem initialized");
}

/*
 * Find shared memory object by name
 */
shm_object_t *shm_find_by_name(const char *name) {
    uint64_t flags = spin_lock_irqsave(&shm_table_lock);

    for (int i = 0; i < MAX_SHM_OBJECTS; i++) {
        if (shm_objects[i] && strcmp(shm_objects[i]->name, name) == 0) {
            shm_object_t *shm = shm_objects[i];
            spin_unlock_irqrestore(&shm_table_lock, flags);
            return shm;
        }
    }

    spin_unlock_irqrestore(&shm_table_lock, flags);
    return NULL;
}

/*
 * Allocate physical memory and map it to kernel virtual address
 */
static int shm_alloc_memory(shm_object_t *shm, size_t size) {
    /* Round up to page size */
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Allocate contiguous physical pages */
    uint64_t phys = alloc_pages(pages);
    if (!phys) {
        ERROR("Failed to allocate %llu pages for shared memory", (unsigned long long)pages);
        return -12;  /* ENOMEM */
    }

    /* Allocate kernel virtual address space */
    uint64_t flags = spin_lock_irqsave(&shm_table_lock);
    uint64_t virt = shm_virt_next;
    shm_virt_next += pages * PAGE_SIZE;
    spin_unlock_irqrestore(&shm_table_lock, flags);

    if (virt + pages * PAGE_SIZE > SHM_VIRT_BASE + SHM_VIRT_SIZE) {
        free_pages(phys, pages);
        ERROR("Shared memory virtual address space exhausted");
        return -12;  /* ENOMEM */
    }

    /* Map physical pages to kernel virtual address */
    for (size_t i = 0; i < pages; i++) {
        uint64_t p = phys + i * PAGE_SIZE;
        uint64_t v = virt + i * PAGE_SIZE;
        if (vmm_map_page(v, p, PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL) != 0) {
            ERROR("Failed to map shared memory page");
            /* Cleanup */
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page(virt + j * PAGE_SIZE);
            }
            free_pages(phys, pages);
            return -12;  /* ENOMEM */
        }
    }

    /* Zero the memory */
    memset((void *)virt, 0, pages * PAGE_SIZE);

    shm->phys_addr = phys;
    shm->virt_addr = virt;
    shm->size = pages * PAGE_SIZE;

    return 0;
}

/*
 * Free shared memory
 */
static void shm_free_memory(shm_object_t *shm) {
    if (!shm->phys_addr || !shm->virt_addr) {
        return;
    }

    size_t pages = shm->size / PAGE_SIZE;

    /* Unmap from kernel virtual address */
    for (size_t i = 0; i < pages; i++) {
        vmm_unmap_page(shm->virt_addr + i * PAGE_SIZE);
    }

    /* Free physical memory */
    free_pages(shm->phys_addr, pages);

    shm->phys_addr = 0;
    shm->virt_addr = 0;
    shm->size = 0;
}

/*
 * Create or open a shared memory object
 */
shm_object_t *shm_open_internal(const char *name, int oflag, mode_t mode) {
    (void)mode;  /* Mode not used for now */

    if (!name || name[0] != '/') {
        return NULL;  /* Name must start with / */
    }

    /* Check if already exists */
    shm_object_t *existing = shm_find_by_name(name);

    if (existing) {
        /* Object exists */
        if (oflag & 0x0200) {  /* O_EXCL */
            return NULL;  /* Already exists and O_EXCL was specified */
        }

        uint64_t flags = spin_lock_irqsave(&existing->lock);
        if (existing->unlinked) {
            spin_unlock_irqrestore(&existing->lock, flags);
            return NULL;  /* Object is being deleted */
        }
        existing->refcount++;
        spin_unlock_irqrestore(&existing->lock, flags);

        return existing;
    }

    /* Create new object if O_CREAT specified */
    if (!(oflag & 0x0100)) {  /* O_CREAT */
        return NULL;  /* Doesn't exist and O_CREAT not specified */
    }

    /* Allocate new shared memory object */
    shm_object_t *shm = kmalloc(sizeof(shm_object_t));
    if (!shm) {
        return NULL;
    }

    memset(shm, 0, sizeof(shm_object_t));
    strncpy(shm->name, name, sizeof(shm->name) - 1);
    shm->name[sizeof(shm->name) - 1] = '\0';
    shm->refcount = 1;
    shm->unlinked = false;
    shm->fd = -1;
    spin_init(&shm->lock);

    /* Add to table */
    uint64_t flags = spin_lock_irqsave(&shm_table_lock);
    for (int i = 0; i < MAX_SHM_OBJECTS; i++) {
        if (!shm_objects[i]) {
            shm_objects[i] = shm;
            spin_unlock_irqrestore(&shm_table_lock, flags);
            return shm;
        }
    }
    spin_unlock_irqrestore(&shm_table_lock, flags);

    /* No free slots */
    kfree(shm);
    return NULL;
}

/*
 * Unlink (delete) a shared memory object
 */
int shm_unlink_internal(const char *name) {
    if (!name || name[0] != '/') {
        return -22;  /* EINVAL */
    }

    shm_object_t *shm = shm_find_by_name(name);
    if (!shm) {
        return -2;  /* ENOENT */
    }

    uint64_t flags = spin_lock_irqsave(&shm->lock);
    shm->unlinked = true;

    /* If no references, remove from table and free */
    if (shm->refcount == 0) {
        spin_unlock_irqrestore(&shm->lock, flags);

        /* Remove from table */
        uint64_t table_flags = spin_lock_irqsave(&shm_table_lock);
        for (int i = 0; i < MAX_SHM_OBJECTS; i++) {
            if (shm_objects[i] == shm) {
                shm_objects[i] = NULL;
                break;
            }
        }
        spin_unlock_irqrestore(&shm_table_lock, table_flags);

        /* Free memory */
        shm_free_memory(shm);
        kfree(shm);
    } else {
        spin_unlock_irqrestore(&shm->lock, flags);
    }

    return 0;
}

/*
 * Increment reference count
 */
void shm_ref(shm_object_t *shm) {
    if (!shm) return;

    uint64_t flags = spin_lock_irqsave(&shm->lock);
    shm->refcount++;
    spin_unlock_irqrestore(&shm->lock, flags);
}

/*
 * Decrement reference count and free if zero
 */
void shm_unref(shm_object_t *shm) {
    if (!shm) return;

    uint64_t flags = spin_lock_irqsave(&shm->lock);
    shm->refcount--;

    /* If unlinked and no more references, free it */
    if (shm->refcount == 0 && shm->unlinked) {
        spin_unlock_irqrestore(&shm->lock, flags);

        /* Remove from table */
        uint64_t table_flags = spin_lock_irqsave(&shm_table_lock);
        for (int i = 0; i < MAX_SHM_OBJECTS; i++) {
            if (shm_objects[i] == shm) {
                shm_objects[i] = NULL;
                break;
            }
        }
        spin_unlock_irqrestore(&shm_table_lock, table_flags);

        /* Free memory */
        shm_free_memory(shm);
        kfree(shm);
    } else {
        spin_unlock_irqrestore(&shm->lock, flags);
    }
}

/*
 * Resize shared memory object (for ftruncate)
 */
int shm_resize(shm_object_t *shm, size_t new_size) {
    if (!shm) {
        return -22;  /* EINVAL */
    }

    uint64_t flags = spin_lock_irqsave(&shm->lock);

    /* If already allocated, can't resize (simplification) */
    if (shm->phys_addr != 0) {
        spin_unlock_irqrestore(&shm->lock, flags);
        return -22;  /* EINVAL - already has memory */
    }

    spin_unlock_irqrestore(&shm->lock, flags);

    /* Allocate memory for the first time */
    return shm_alloc_memory(shm, new_size);
}

/*
 * Syscall wrapper for shm_open
 * Creates/opens a shared memory object and returns a file descriptor
 */
int shm_open_syscall(const char *name, int oflag, mode_t mode) {
    /* Open the shared memory object */
    shm_object_t *shm = shm_open_internal(name, oflag, mode);
    if (!shm) {
        return -2;  /* ENOENT or other error */
    }

    /* For now, return a fake fd. In a full implementation, this would:
     * 1. Create a VFS node for the shm object
     * 2. Allocate a file descriptor
     * 3. Associate the fd with the shm object
     * For simplicity, we'll just store the shm pointer in a static array
     * and return an index as the fd.
     */

    /* FIXME: This is a simplified implementation. A proper implementation
     * would integrate with the VFS and fd_table system. */
    static shm_object_t *shm_fd_table[64];
    static spinlock_t shm_fd_lock = SPINLOCK_INIT;

    uint64_t flags = spin_lock_irqsave(&shm_fd_lock);
    for (int i = 0; i < 64; i++) {
        if (!shm_fd_table[i]) {
            shm_fd_table[i] = shm;
            shm->fd = i + 1000;  /* Offset to avoid collision with regular fds */
            spin_unlock_irqrestore(&shm_fd_lock, flags);
            return shm->fd;
        }
    }
    spin_unlock_irqrestore(&shm_fd_lock, flags);

    /* No free slots */
    shm_unref(shm);
    return -24;  /* EMFILE */
}

/*
 * Syscall wrapper for shm_unlink
 */
int shm_unlink_syscall(const char *name) {
    return shm_unlink_internal(name);
}
