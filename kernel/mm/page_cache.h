/* page_cache.h - Page Cache for File-backed Memory */
#ifndef _KERNEL_MM_PAGE_CACHE_H
#define _KERNEL_MM_PAGE_CACHE_H

#include <stdint.h>
#include <stdbool.h>
#include "../fs/vfs.h"

/*
 * Page cache entry
 * Represents a single cached page from a file
 */
typedef struct page_cache_entry {
    vfs_node_t *file;               /* File this page belongs to */
    uint64_t offset;                /* Offset in file (page-aligned) */
    uint64_t phys;                  /* Physical address of cached data */
    bool dirty;                     /* Page has been modified */
    uint32_t refcount;              /* Number of mappings using this page */
    struct page_cache_entry *hash_next;  /* Next entry in hash bucket */
    struct page_cache_entry *lru_prev;   /* LRU list for eviction */
    struct page_cache_entry *lru_next;
} page_cache_entry_t;

/*
 * Initialize the page cache subsystem
 */
void page_cache_init(void);

/*
 * Look up a page in the cache
 *
 * @param file    File to look up
 * @param offset  Offset in file (will be page-aligned)
 * @return Physical address of cached page, or 0 if not cached
 */
uint64_t page_cache_lookup(vfs_node_t *file, uint64_t offset);

/*
 * Insert a page into the cache
 *
 * @param file    File the page belongs to
 * @param offset  Offset in file (will be page-aligned)
 * @param phys    Physical address of the page
 * @return 0 on success, negative error on failure
 */
int page_cache_insert(vfs_node_t *file, uint64_t offset, uint64_t phys);

/*
 * Mark a cached page as dirty
 *
 * @param file    File containing the page
 * @param offset  Offset in file
 */
void page_cache_mark_dirty(vfs_node_t *file, uint64_t offset);

/*
 * Sync dirty pages for a file back to disk
 *
 * @param file  File to sync (NULL = sync all files)
 * @return Number of pages synced, or negative error
 */
int page_cache_sync(vfs_node_t *file);

/*
 * Invalidate cached pages for a file
 *
 * @param file  File to invalidate
 */
void page_cache_invalidate(vfs_node_t *file);

/*
 * Get a page from the cache, reading from disk if needed
 *
 * @param file    File to read from
 * @param offset  Offset in file (will be page-aligned)
 * @return Physical address of page, or 0 on error
 */
uint64_t page_cache_get(vfs_node_t *file, uint64_t offset);

/*
 * Release a page cache entry (decrement refcount)
 *
 * @param file    File containing the page
 * @param offset  Offset in file
 */
void page_cache_put(vfs_node_t *file, uint64_t offset);

/*
 * Increment page cache refcount
 *
 * @param file    File containing the page
 * @param offset  Offset in file
 */
void page_cache_ref(vfs_node_t *file, uint64_t offset);

#endif /* _KERNEL_MM_PAGE_CACHE_H */
