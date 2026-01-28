/* fd_table.h - Per-process file descriptor table */
#ifndef _KERNEL_FD_TABLE_H
#define _KERNEL_FD_TABLE_H

#include <stdint.h>
#include <stdbool.h>
#include "vfs.h"

/*
 * Maximum file descriptors per process
 */
#define FD_MAX  256

/*
 * File descriptor flags
 */
#define FD_CLOEXEC  0x01    /* Close on exec */

/*
 * File descriptor entry
 */
typedef struct fd_entry {
    file_t *file;           /* Pointer to open file (NULL = unused) */
    uint32_t flags;         /* FD flags (FD_CLOEXEC, etc.) */
} fd_entry_t;

/*
 * Per-process file descriptor table
 */
typedef struct fd_table {
    fd_entry_t entries[FD_MAX];
    uint32_t ref_count;     /* For sharing between processes */
} fd_table_t;

/*
 * Create a new fd table
 * All entries are initialized to unused (file = NULL)
 *
 * @return New fd table, or NULL on failure
 */
fd_table_t *fd_table_create(void);

/*
 * Destroy an fd table and close all open files
 *
 * @param fdt  Table to destroy
 */
void fd_table_destroy(fd_table_t *fdt);

/*
 * Duplicate an fd table (for fork)
 * Creates a new table with same files (ref counts incremented)
 *
 * @param src  Source table to duplicate
 * @return New duplicated table, or NULL on failure
 */
fd_table_t *fd_table_clone(fd_table_t *src);

/*
 * Close all FD_CLOEXEC file descriptors (for exec)
 *
 * @param fdt  Table to process
 */
void fd_table_close_cloexec(fd_table_t *fdt);

/*
 * Allocate a file descriptor in a table
 *
 * @param fdt   File descriptor table
 * @param file  File to associate with fd
 * @param flags FD flags (FD_CLOEXEC, etc.)
 * @return File descriptor number, or -1 on failure
 */
int fd_table_alloc(fd_table_t *fdt, file_t *file, uint32_t flags);

/*
 * Allocate a specific file descriptor
 *
 * @param fdt   File descriptor table
 * @param fd    Specific fd to allocate
 * @param file  File to associate
 * @param flags FD flags
 * @return fd on success, -1 on failure (fd in use)
 */
int fd_table_alloc_at(fd_table_t *fdt, int fd, file_t *file, uint32_t flags);

/*
 * Free a file descriptor (does not close the file)
 *
 * @param fdt  File descriptor table
 * @param fd   File descriptor to free
 * @return The file_t that was associated, or NULL
 */
file_t *fd_table_free(fd_table_t *fdt, int fd);

/*
 * Get file for a descriptor
 *
 * @param fdt  File descriptor table
 * @param fd   File descriptor
 * @return file_t pointer, or NULL if invalid
 */
file_t *fd_table_get(fd_table_t *fdt, int fd);

/*
 * Get fd flags
 *
 * @param fdt  File descriptor table
 * @param fd   File descriptor
 * @return Flags, or -1 if invalid
 */
int fd_table_get_flags(fd_table_t *fdt, int fd);

/*
 * Set fd flags
 *
 * @param fdt   File descriptor table
 * @param fd    File descriptor
 * @param flags New flags
 * @return 0 on success, -1 on failure
 */
int fd_table_set_flags(fd_table_t *fdt, int fd, uint32_t flags);

#endif /* _KERNEL_FD_TABLE_H */
