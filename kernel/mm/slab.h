/* slab.h - Slab Allocator for Kernel Heap */
#ifndef _KERNEL_MM_SLAB_H
#define _KERNEL_MM_SLAB_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Slab Allocator Overview:
 *
 * The slab allocator provides efficient memory allocation for kernel objects.
 * - Object caches: fixed-size allocations for specific object types
 * - kmalloc/kfree: general-purpose allocation using size-based caches
 *
 * Structure:
 * - Cache: manages slabs of a specific object size
 * - Slab: one or more pages containing fixed-size objects
 * - Object: individual allocation unit within a slab
 */

/* Slab configuration */
#define SLAB_MIN_SIZE       16      /* Minimum object size */
#define SLAB_MAX_SIZE       4096    /* Maximum object for slab (larger uses pages) */
#define KMALLOC_MAX_CACHE   12      /* Number of kmalloc caches (16 to 4096 bytes) */

/* Slab flags */
#define SLAB_CACHE_DMA      (1 << 0)    /* Allocate from DMA-able memory */
#define SLAB_PANIC          (1 << 1)    /* Panic on allocation failure */
#define SLAB_ZERO           (1 << 2)    /* Zero memory on allocation */

/*
 * Free object header - stored at the start of each free object
 * When an object is allocated, this space is available for user data
 */
typedef struct slab_obj {
    struct slab_obj *next;      /* Next free object in slab */
} slab_obj_t;

/*
 * Slab descriptor - manages a single slab (one or more pages)
 * Stored at the beginning of the slab itself for small objects
 */
typedef struct slab {
    struct slab *next;          /* Next slab in list (full/partial/empty) */
    struct slab *prev;          /* Previous slab in list */
    struct kmem_cache *cache;   /* Owning cache */
    slab_obj_t *free_list;      /* List of free objects */
    uint32_t in_use;            /* Number of objects in use */
    uint32_t total;             /* Total objects in slab */
    void *objects;              /* Start of object array */
} slab_t;

/*
 * Slab cache - manages all slabs of a particular object size
 */
typedef struct kmem_cache {
    const char *name;           /* Cache name (for debugging) */
    size_t obj_size;            /* Size of each object */
    size_t align;               /* Object alignment */
    size_t slab_size;           /* Size of each slab (pages) */
    uint32_t objs_per_slab;     /* Objects per slab */
    uint32_t flags;             /* Cache flags */

    /* Slab lists */
    slab_t *slabs_full;         /* Fully allocated slabs */
    slab_t *slabs_partial;      /* Partially allocated slabs */
    slab_t *slabs_empty;        /* Empty slabs (cache for reuse) */

    /* Statistics */
    uint64_t alloc_count;       /* Total allocations */
    uint64_t free_count;        /* Total frees */
    uint64_t slab_count;        /* Current number of slabs */

    /* Cache list linkage */
    struct kmem_cache *next;    /* Next cache in global list */
} kmem_cache_t;

/*
 * Initialize the slab allocator
 * Must be called after PMM and VMM are initialized
 */
void slab_init(void);

/*
 * Create a new slab cache for objects of a specific size
 * name: cache name for debugging
 * size: object size in bytes
 * align: alignment requirement (0 for default)
 * flags: cache flags
 * Returns: pointer to cache, or NULL on failure
 */
kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                 size_t align, uint32_t flags);

/*
 * Destroy a slab cache
 * All objects must be freed before calling this
 */
void kmem_cache_destroy(kmem_cache_t *cache);

/*
 * Allocate an object from a cache
 * Returns: pointer to object, or NULL on failure
 */
void *kmem_cache_alloc(kmem_cache_t *cache);

/*
 * Free an object back to its cache
 */
void kmem_cache_free(kmem_cache_t *cache, void *obj);

/*
 * General-purpose allocation (like malloc)
 * size: number of bytes to allocate
 * Returns: pointer to memory, or NULL on failure
 */
void *kmalloc(size_t size);

/*
 * Allocate zeroed memory
 */
void *kzalloc(size_t size);

/*
 * Free memory allocated by kmalloc
 */
void kfree(void *ptr);

/*
 * Get the size of an allocation
 * Returns the usable size (may be larger than requested)
 */
size_t ksize(void *ptr);

/*
 * Debug: print cache statistics
 */
void kmem_cache_dump(kmem_cache_t *cache);

/*
 * Debug: print all cache statistics
 */
void slab_dump_stats(void);

#endif /* _KERNEL_MM_SLAB_H */
