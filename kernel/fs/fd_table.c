/* fd_table.c - Per-process file descriptor table */

#include "fd_table.h"
#include "vfs.h"
#include "../mm/slab.h"
#include "../debug/debug.h"
#include "../lib/string.h"

/* Slab cache for fd tables */
static kmem_cache_t *fdt_cache = NULL;

/*
 * Initialize fd table subsystem (called from vfs_init)
 */
void fd_table_init(void) {
    if (!fdt_cache) {
        fdt_cache = kmem_cache_create("fd_table", sizeof(fd_table_t), 8, SLAB_ZERO);
        if (!fdt_cache) {
            panic("Failed to create fd_table slab cache");
        }
    }
}

/*
 * Create a new fd table
 */
fd_table_t *fd_table_create(void) {
    /* Ensure initialized */
    if (!fdt_cache) {
        fd_table_init();
    }

    fd_table_t *fdt = kmem_cache_alloc(fdt_cache);
    if (!fdt) {
        ERROR("fd_table_create: failed to allocate");
        return NULL;
    }

    memset(fdt, 0, sizeof(fd_table_t));
    fdt->ref_count = 1;

    DEBUG("fd_table_create: created at %p", fdt);
    return fdt;
}

/*
 * Destroy an fd table
 */
void fd_table_destroy(fd_table_t *fdt) {
    if (!fdt) {
        return;
    }

    fdt->ref_count--;
    if (fdt->ref_count > 0) {
        DEBUG("fd_table_destroy: ref_count decremented to %u", fdt->ref_count);
        return;
    }

    DEBUG("fd_table_destroy: destroying at %p", fdt);

    /* Close all open files */
    for (int i = 0; i < FD_MAX; i++) {
        if (fdt->entries[i].file) {
            file_t *file = fdt->entries[i].file;
            fdt->entries[i].file = NULL;

            /* Decrement file ref count and close if needed */
            if (file->ref_count > 0) {
                file->ref_count--;
            }
            if (file->ref_count == 0) {
                /* Close the underlying node */
                if (file->node) {
                    if (file->node->ops && file->node->ops->close) {
                        file->node->ops->close(file->node);
                    }
                    file->node->open_count--;
                    vfs_node_unref(file->node);
                }
                /* Free the file structure - use kfree for now */
                kfree(file);
            }
        }
    }

    kmem_cache_free(fdt_cache, fdt);
}

/*
 * Clone an fd table (for fork)
 */
fd_table_t *fd_table_clone(fd_table_t *src) {
    if (!src) {
        return fd_table_create();
    }

    fd_table_t *dst = fd_table_create();
    if (!dst) {
        return NULL;
    }

    /* Copy all entries, incrementing file ref counts */
    for (int i = 0; i < FD_MAX; i++) {
        if (src->entries[i].file) {
            dst->entries[i].file = src->entries[i].file;
            dst->entries[i].flags = src->entries[i].flags;
            dst->entries[i].file->ref_count++;

            /* Also increment node ref count */
            if (dst->entries[i].file->node) {
                vfs_node_ref(dst->entries[i].file->node);
            }
        }
    }

    DEBUG("fd_table_clone: cloned %p to %p", src, dst);
    return dst;
}

/*
 * Close all FD_CLOEXEC descriptors (for exec)
 */
void fd_table_close_cloexec(fd_table_t *fdt) {
    if (!fdt) {
        return;
    }

    int closed = 0;
    for (int i = 0; i < FD_MAX; i++) {
        if (fdt->entries[i].file && (fdt->entries[i].flags & FD_CLOEXEC)) {
            file_t *file = fd_table_free(fdt, i);
            if (file) {
                /* Decrement ref count */
                if (file->ref_count > 0) {
                    file->ref_count--;
                }
                if (file->ref_count == 0) {
                    if (file->node) {
                        if (file->node->ops && file->node->ops->close) {
                            file->node->ops->close(file->node);
                        }
                        file->node->open_count--;
                        vfs_node_unref(file->node);
                    }
                    kfree(file);
                }
                closed++;
            }
        }
    }

    DEBUG("fd_table_close_cloexec: closed %d descriptors", closed);
}

/*
 * Allocate a file descriptor
 */
int fd_table_alloc(fd_table_t *fdt, file_t *file, uint32_t flags) {
    if (!fdt || !file) {
        return -1;
    }

    /* Find lowest available fd */
    for (int i = 0; i < FD_MAX; i++) {
        if (!fdt->entries[i].file) {
            fdt->entries[i].file = file;
            fdt->entries[i].flags = flags;
            return i;
        }
    }

    return -1;  /* Table full */
}

/*
 * Allocate a specific fd
 */
int fd_table_alloc_at(fd_table_t *fdt, int fd, file_t *file, uint32_t flags) {
    if (!fdt || !file || fd < 0 || fd >= FD_MAX) {
        return -1;
    }

    if (fdt->entries[fd].file) {
        return -1;  /* Already in use */
    }

    fdt->entries[fd].file = file;
    fdt->entries[fd].flags = flags;
    return fd;
}

/*
 * Free a file descriptor
 */
file_t *fd_table_free(fd_table_t *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= FD_MAX) {
        return NULL;
    }

    file_t *file = fdt->entries[fd].file;
    fdt->entries[fd].file = NULL;
    fdt->entries[fd].flags = 0;
    return file;
}

/*
 * Get file for a descriptor
 */
file_t *fd_table_get(fd_table_t *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= FD_MAX) {
        return NULL;
    }
    return fdt->entries[fd].file;
}

/*
 * Get fd flags
 */
int fd_table_get_flags(fd_table_t *fdt, int fd) {
    if (!fdt || fd < 0 || fd >= FD_MAX || !fdt->entries[fd].file) {
        return -1;
    }
    return (int)fdt->entries[fd].flags;
}

/*
 * Set fd flags
 */
int fd_table_set_flags(fd_table_t *fdt, int fd, uint32_t flags) {
    if (!fdt || fd < 0 || fd >= FD_MAX || !fdt->entries[fd].file) {
        return -1;
    }
    fdt->entries[fd].flags = flags;
    return 0;
}
