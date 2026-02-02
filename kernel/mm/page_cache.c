/* page_cache.c - Page Cache Implementation */

#include "page_cache.h"
#include "pmm.h"
#include "vmm.h"
#include "slab.h"
#include "../debug/debug.h"
#include "../sync/spinlock.h"
#include "../lib/string.h"

/* Page cache hash table size (must be power of 2) */
#define PAGE_CACHE_HASH_SIZE    256
#define PAGE_CACHE_HASH_MASK    (PAGE_CACHE_HASH_SIZE - 1)

/* Hash table for page cache lookups */
static page_cache_entry_t *page_cache_hash[PAGE_CACHE_HASH_SIZE];

/* Slab cache for page_cache_entry_t */
static kmem_cache_t *page_cache_entry_cache;

/* Lock for page cache operations */
static spinlock_t page_cache_lock = SPINLOCK_INIT;

/* LRU list for eviction (not fully implemented yet) */
static page_cache_entry_t *lru_head;
static page_cache_entry_t *lru_tail;

/* Statistics */
static uint64_t cache_hits;
static uint64_t cache_misses;
static uint64_t cache_entries;

/*
 * Hash function for (file, offset) pair
 */
static inline uint32_t page_cache_hash_fn(vfs_node_t *file, uint64_t offset) {
    uint64_t key = (uint64_t)file ^ (offset >> 12);
    key = key * 0x9e3779b97f4a7c15ULL;  /* Golden ratio hash */
    return (uint32_t)(key & PAGE_CACHE_HASH_MASK);
}

/*
 * Convert physical to virtual for kernel access
 */
static inline void* phys_to_virt(uint64_t phys) {
    if (phys >= KERNEL_PHYS_BASE && phys < KERNEL_PHYS_BASE + 0x8000000) {
        return (void*)KERNEL_PHYS_TO_VIRT(phys);
    }
    return (void*)phys;
}

/*
 * Initialize page cache
 */
void page_cache_init(void) {
    INFO("Initializing page cache");

    /* Create slab cache for entries */
    page_cache_entry_cache = kmem_cache_create("page_cache_entry",
                                                sizeof(page_cache_entry_t), 0, 0);
    if (!page_cache_entry_cache) {
        panic("Failed to create page cache entry cache");
    }

    /* Clear hash table */
    for (int i = 0; i < PAGE_CACHE_HASH_SIZE; i++) {
        page_cache_hash[i] = NULL;
    }

    lru_head = NULL;
    lru_tail = NULL;
    cache_hits = 0;
    cache_misses = 0;
    cache_entries = 0;

    INFO("Page cache initialized");
}

/*
 * Find entry in cache (caller must hold lock)
 */
static page_cache_entry_t *find_entry_locked(vfs_node_t *file, uint64_t offset) {
    offset &= ~(PAGE_SIZE - 1);  /* Page-align */

    uint32_t hash = page_cache_hash_fn(file, offset);
    page_cache_entry_t *entry = page_cache_hash[hash];

    while (entry) {
        if (entry->file == file && entry->offset == offset) {
            return entry;
        }
        entry = entry->hash_next;
    }
    return NULL;
}

/*
 * Look up a page in the cache
 */
uint64_t page_cache_lookup(vfs_node_t *file, uint64_t offset) {
    if (!file) return 0;

    uint64_t flags = spin_lock_irqsave(&page_cache_lock);

    page_cache_entry_t *entry = find_entry_locked(file, offset);
    uint64_t phys = 0;

    if (entry) {
        phys = entry->phys;
        cache_hits++;
    } else {
        cache_misses++;
    }

    spin_unlock_irqrestore(&page_cache_lock, flags);
    return phys;
}

/*
 * Insert a page into the cache
 */
int page_cache_insert(vfs_node_t *file, uint64_t offset, uint64_t phys) {
    if (!file || !phys) return -22;  /* EINVAL */

    offset &= ~(PAGE_SIZE - 1);  /* Page-align */

    uint64_t flags = spin_lock_irqsave(&page_cache_lock);

    /* Check if already cached */
    page_cache_entry_t *existing = find_entry_locked(file, offset);
    if (existing) {
        spin_unlock_irqrestore(&page_cache_lock, flags);
        return -17;  /* EEXIST */
    }

    /* Allocate entry */
    page_cache_entry_t *entry = kmem_cache_alloc(page_cache_entry_cache);
    if (!entry) {
        spin_unlock_irqrestore(&page_cache_lock, flags);
        return -12;  /* ENOMEM */
    }

    entry->file = file;
    entry->offset = offset;
    entry->phys = phys;
    entry->dirty = false;
    entry->refcount = 1;
    entry->lru_prev = NULL;
    entry->lru_next = NULL;

    /* Insert into hash table */
    uint32_t hash = page_cache_hash_fn(file, offset);
    entry->hash_next = page_cache_hash[hash];
    page_cache_hash[hash] = entry;

    /* Add to LRU list (at head = most recently used) */
    entry->lru_next = lru_head;
    if (lru_head) {
        lru_head->lru_prev = entry;
    }
    lru_head = entry;
    if (!lru_tail) {
        lru_tail = entry;
    }

    cache_entries++;

    spin_unlock_irqrestore(&page_cache_lock, flags);

    DEBUG("Page cache: inserted file=%p offset=0x%llx phys=0x%llx",
          file, offset, phys);
    return 0;
}

/*
 * Mark a cached page as dirty
 */
void page_cache_mark_dirty(vfs_node_t *file, uint64_t offset) {
    if (!file) return;

    uint64_t flags = spin_lock_irqsave(&page_cache_lock);

    page_cache_entry_t *entry = find_entry_locked(file, offset);
    if (entry) {
        entry->dirty = true;
    }

    spin_unlock_irqrestore(&page_cache_lock, flags);
}

/*
 * Sync dirty pages back to disk
 */
int page_cache_sync(vfs_node_t *file) {
    int synced = 0;

    uint64_t flags = spin_lock_irqsave(&page_cache_lock);

    /* Walk all hash buckets */
    for (int i = 0; i < PAGE_CACHE_HASH_SIZE; i++) {
        page_cache_entry_t *entry = page_cache_hash[i];
        while (entry) {
            if (entry->dirty && (!file || entry->file == file)) {
                /* Write back to file */
                if (entry->file && entry->file->ops && entry->file->ops->write) {
                    void *data = phys_to_virt(entry->phys);
                    /* Release lock during I/O */
                    spin_unlock_irqrestore(&page_cache_lock, flags);

                    ssize_t written = entry->file->ops->write(entry->file, data,
                                                              PAGE_SIZE, entry->offset);
                    flags = spin_lock_irqsave(&page_cache_lock);

                    if (written > 0) {
                        entry->dirty = false;
                        synced++;
                    }
                }
            }
            entry = entry->hash_next;
        }
    }

    spin_unlock_irqrestore(&page_cache_lock, flags);

    DEBUG("Page cache: synced %d pages", synced);
    return synced;
}

/*
 * Invalidate cached pages for a file
 */
void page_cache_invalidate(vfs_node_t *file) {
    if (!file) return;

    uint64_t flags = spin_lock_irqsave(&page_cache_lock);

    /* Walk all hash buckets and remove entries for this file */
    for (int i = 0; i < PAGE_CACHE_HASH_SIZE; i++) {
        page_cache_entry_t **pp = &page_cache_hash[i];
        while (*pp) {
            page_cache_entry_t *entry = *pp;
            if (entry->file == file) {
                /* Remove from hash */
                *pp = entry->hash_next;

                /* Remove from LRU */
                if (entry->lru_prev) {
                    entry->lru_prev->lru_next = entry->lru_next;
                } else {
                    lru_head = entry->lru_next;
                }
                if (entry->lru_next) {
                    entry->lru_next->lru_prev = entry->lru_prev;
                } else {
                    lru_tail = entry->lru_prev;
                }

                /* Free physical page if no other refs */
                if (entry->refcount <= 1) {
                    free_page(entry->phys);
                }

                kmem_cache_free(page_cache_entry_cache, entry);
                cache_entries--;
            } else {
                pp = &entry->hash_next;
            }
        }
    }

    spin_unlock_irqrestore(&page_cache_lock, flags);
}

/*
 * Get a page from cache, reading from disk if needed
 */
uint64_t page_cache_get(vfs_node_t *file, uint64_t offset) {
    if (!file) return 0;

    offset &= ~(PAGE_SIZE - 1);

    /* Check cache first */
    uint64_t phys = page_cache_lookup(file, offset);
    if (phys) {
        page_cache_ref(file, offset);
        return phys;
    }

    /* Not cached - allocate page and read from file */
    phys = alloc_page();
    if (!phys) {
        ERROR("page_cache_get: failed to allocate page");
        return 0;
    }

    void *data = phys_to_virt(phys);

    /* Read from file */
    if (file->ops && file->ops->read) {
        ssize_t read = file->ops->read(file, data, PAGE_SIZE, offset);
        if (read < 0) {
            ERROR("page_cache_get: read failed for offset 0x%llx", offset);
            free_page(phys);
            return 0;
        }
        /* Zero the rest if partial read */
        if ((uint64_t)read < PAGE_SIZE) {
            memset((char*)data + read, 0, PAGE_SIZE - read);
        }
    } else {
        /* No read op - zero fill */
        memset(data, 0, PAGE_SIZE);
    }

    /* Insert into cache */
    if (page_cache_insert(file, offset, phys) < 0) {
        /* Insert failed - but we can still return the page */
        DEBUG("page_cache_get: failed to insert into cache");
    }

    return phys;
}

/*
 * Release a page cache entry
 */
void page_cache_put(vfs_node_t *file, uint64_t offset) {
    if (!file) return;

    offset &= ~(PAGE_SIZE - 1);

    uint64_t flags = spin_lock_irqsave(&page_cache_lock);

    page_cache_entry_t *entry = find_entry_locked(file, offset);
    if (entry && entry->refcount > 0) {
        entry->refcount--;
    }

    spin_unlock_irqrestore(&page_cache_lock, flags);
}

/*
 * Increment page cache refcount
 */
void page_cache_ref(vfs_node_t *file, uint64_t offset) {
    if (!file) return;

    offset &= ~(PAGE_SIZE - 1);

    uint64_t flags = spin_lock_irqsave(&page_cache_lock);

    page_cache_entry_t *entry = find_entry_locked(file, offset);
    if (entry) {
        entry->refcount++;
    }

    spin_unlock_irqrestore(&page_cache_lock, flags);
}
