/* slab.c - Slab Allocator for Kernel Heap */

#include "slab.h"
#include "pmm.h"
#include "vmm.h"
#include "../debug/debug.h"
#include "../include/types.h"

/* Kernel heap virtual address range */
#define KHEAP_START     0xFFFFFFFF88000000UL
#define KHEAP_END       0xFFFFFFFFC0000000UL

/* Current heap allocation pointer */
static uint64_t heap_current = KHEAP_START;

/* Global list of all caches */
static kmem_cache_t *cache_list = NULL;

/* kmalloc caches for power-of-2 sizes: 16, 32, 64, 128, 256, 512, 1K, 2K, 4K */
static kmem_cache_t *kmalloc_caches[KMALLOC_MAX_CACHE];
static const size_t kmalloc_sizes[KMALLOC_MAX_CACHE] = {
    16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
};
static const char *kmalloc_names[KMALLOC_MAX_CACHE] = {
    "kmalloc-16", "kmalloc-32", "kmalloc-64", "kmalloc-128",
    "kmalloc-256", "kmalloc-512", "kmalloc-1024", "kmalloc-2048",
    "kmalloc-4096", "kmalloc-8192", "kmalloc-16384", "kmalloc-32768"
};

/* Forward declarations */
static slab_t *slab_create(kmem_cache_t *cache);
static void slab_destroy(kmem_cache_t *cache, slab_t *slab);
static void *slab_alloc_obj(slab_t *slab);
static void slab_free_obj(slab_t *slab, void *obj);
static void slab_list_add(slab_t **list, slab_t *slab);
static void slab_list_remove(slab_t **list, slab_t *slab);

/*
 * Allocate virtual address space for heap
 * Maps physical pages and returns virtual address
 */
static void *heap_alloc_pages(size_t count) {
    DEBUG("heap_alloc_pages(%llu): heap_current=0x%llx", (unsigned long long)count, (unsigned long long)heap_current);

    if (heap_current + (count * PAGE_SIZE) > KHEAP_END) {
        ERROR("Kernel heap exhausted!");
        return NULL;
    }

    uint64_t virt = heap_current;

    for (size_t i = 0; i < count; i++) {
        uint64_t phys = alloc_page();
        DEBUG("  alloc_page() returned 0x%llx", (unsigned long long)phys);
        if (phys == 0) {
            /* Allocation failed - unmap and free what we got */
            for (size_t j = 0; j < i; j++) {
                uint64_t p = vmm_unmap_page(virt + j * PAGE_SIZE);
                if (p) free_page(p);
            }
            ERROR("Failed to allocate physical page for heap");
            return NULL;
        }

        DEBUG("  mapping virt 0x%llx -> phys 0x%llx", (unsigned long long)(virt + i * PAGE_SIZE), (unsigned long long)phys);
        if (vmm_map_page(virt + i * PAGE_SIZE, phys, PTE_KERNEL_RW) != 0) {
            free_page(phys);
            for (size_t j = 0; j < i; j++) {
                uint64_t p = vmm_unmap_page(virt + j * PAGE_SIZE);
                if (p) free_page(p);
            }
            ERROR("Failed to map heap page");
            return NULL;
        }
        DEBUG("  mapped successfully");
    }

    heap_current += count * PAGE_SIZE;
    DEBUG("  returning 0x%llx", (unsigned long long)virt);
    return (void *)virt;
}

/*
 * Free heap pages back to system
 */
static void heap_free_pages(void *ptr, size_t count) {
    uint64_t virt = (uint64_t)ptr;

    for (size_t i = 0; i < count; i++) {
        uint64_t phys = vmm_unmap_page(virt + i * PAGE_SIZE);
        if (phys) {
            free_page(phys);
        }
    }
    /* Note: doesn't reclaim virtual address space - acceptable for kernel heap */
}

/*
 * Calculate objects per slab
 */
static uint32_t calc_objs_per_slab(size_t obj_size, size_t slab_pages) {
    size_t slab_size = slab_pages * PAGE_SIZE;
    size_t usable = slab_size - sizeof(slab_t);
    return (uint32_t)(usable / obj_size);
}

/*
 * Initialize the slab allocator
 */
void slab_init(void) {
    INFO("Initializing slab allocator...");

    /* Explicitly initialize heap_current in case static init failed */
    heap_current = KHEAP_START;

    DEBUG("Heap start: 0x%llx, end: 0x%llx", (unsigned long long)KHEAP_START, (unsigned long long)KHEAP_END);

    /* Create kmalloc caches for each size class */
    for (int i = 0; i < KMALLOC_MAX_CACHE; i++) {
        DEBUG("Creating cache %d: %s (size %llu)", i, kmalloc_names[i], (unsigned long long)kmalloc_sizes[i]);
        kmalloc_caches[i] = kmem_cache_create(kmalloc_names[i], kmalloc_sizes[i], 0, 0);
        if (!kmalloc_caches[i]) {
            panic("Failed to create kmalloc cache for size %llu", (unsigned long long)kmalloc_sizes[i]);
        }
        DEBUG("Cache %d created at %p", i, kmalloc_caches[i]);
    }

    INFO("Slab allocator initialized with %d size classes", KMALLOC_MAX_CACHE);
}

/*
 * Create a new slab cache
 */
kmem_cache_t *kmem_cache_create(const char *name, size_t size,
                                 size_t align, uint32_t flags) {
    /* Enforce minimum size and alignment */
    if (size < SLAB_MIN_SIZE) {
        size = SLAB_MIN_SIZE;
    }

    if (align == 0) {
        /* Default alignment: next power of 2 up to 8, or size if larger */
        align = 8;
        if (size > 8 && size <= 16) align = 16;
        else if (size > 16) align = 16;  /* Cap at 16-byte alignment */
    }

    /* Round size up to alignment */
    size = ALIGN_UP(size, align);

    /* Allocate cache descriptor */
    kmem_cache_t *cache = heap_alloc_pages(1);
    if (!cache) {
        return NULL;
    }

    /* Zero the cache structure */
    uint8_t *p = (uint8_t *)cache;
    for (size_t i = 0; i < sizeof(kmem_cache_t); i++) {
        p[i] = 0;
    }

    cache->name = name;
    cache->obj_size = size;
    cache->align = align;
    cache->flags = flags;

    /* Determine slab size - use 1 page for small objects, more for larger */
    if (size <= 256) {
        cache->slab_size = 1;
    } else if (size <= 1024) {
        cache->slab_size = 2;
    } else {
        cache->slab_size = 4;
    }

    cache->objs_per_slab = calc_objs_per_slab(size, cache->slab_size);

    /* Ensure at least 2 objects per slab */
    while (cache->objs_per_slab < 2 && cache->slab_size < 16) {
        cache->slab_size *= 2;
        cache->objs_per_slab = calc_objs_per_slab(size, cache->slab_size);
    }

    /* Add to global cache list */
    cache->next = cache_list;
    cache_list = cache;

    TRACE("Created cache '%s': obj_size=%llu, objs_per_slab=%u, slab_pages=%llu",
          name, (unsigned long long)size, cache->objs_per_slab, (unsigned long long)cache->slab_size);

    return cache;
}

/*
 * Destroy a slab cache
 */
void kmem_cache_destroy(kmem_cache_t *cache) {
    if (!cache) return;

    /* Free all slabs */
    slab_t *slab, *next;

    for (slab = cache->slabs_full; slab; slab = next) {
        next = slab->next;
        slab_destroy(cache, slab);
    }

    for (slab = cache->slabs_partial; slab; slab = next) {
        next = slab->next;
        slab_destroy(cache, slab);
    }

    for (slab = cache->slabs_empty; slab; slab = next) {
        next = slab->next;
        slab_destroy(cache, slab);
    }

    /* Remove from global list */
    if (cache_list == cache) {
        cache_list = cache->next;
    } else {
        for (kmem_cache_t *c = cache_list; c; c = c->next) {
            if (c->next == cache) {
                c->next = cache->next;
                break;
            }
        }
    }

    /* Free cache descriptor */
    heap_free_pages(cache, 1);
}

/*
 * Create a new slab for a cache
 */
static slab_t *slab_create(kmem_cache_t *cache) {
    /* Allocate pages for slab */
    void *mem = heap_alloc_pages(cache->slab_size);
    if (!mem) {
        return NULL;
    }

    /* Slab descriptor at start of memory */
    slab_t *slab = (slab_t *)mem;
    slab->next = NULL;
    slab->prev = NULL;
    slab->cache = cache;
    slab->in_use = 0;
    slab->total = cache->objs_per_slab;

    /* Objects start after slab descriptor, aligned */
    uint64_t obj_start = (uint64_t)mem + sizeof(slab_t);
    obj_start = ALIGN_UP(obj_start, cache->align);
    slab->objects = (void *)obj_start;

    /* Initialize free list - chain all objects together */
    slab->free_list = NULL;
    uint8_t *obj = (uint8_t *)slab->objects;

    for (uint32_t i = 0; i < slab->total; i++) {
        slab_obj_t *free_obj = (slab_obj_t *)obj;
        free_obj->next = slab->free_list;
        slab->free_list = free_obj;
        obj += cache->obj_size;
    }

    cache->slab_count++;

    return slab;
}

/*
 * Destroy a slab
 */
static void slab_destroy(kmem_cache_t *cache, slab_t *slab) {
    heap_free_pages(slab, cache->slab_size);
    cache->slab_count--;
}

/*
 * Add slab to a list
 */
static void slab_list_add(slab_t **list, slab_t *slab) {
    slab->prev = NULL;
    slab->next = *list;
    if (*list) {
        (*list)->prev = slab;
    }
    *list = slab;
}

/*
 * Remove slab from a list
 */
static void slab_list_remove(slab_t **list, slab_t *slab) {
    if (slab->prev) {
        slab->prev->next = slab->next;
    } else {
        *list = slab->next;
    }
    if (slab->next) {
        slab->next->prev = slab->prev;
    }
    slab->prev = NULL;
    slab->next = NULL;
}

/*
 * Allocate an object from a slab
 */
static void *slab_alloc_obj(slab_t *slab) {
    if (!slab->free_list) {
        return NULL;
    }

    slab_obj_t *obj = slab->free_list;
    slab->free_list = obj->next;
    slab->in_use++;

    return (void *)obj;
}

/*
 * Free an object back to a slab
 */
static void slab_free_obj(slab_t *slab, void *obj) {
    slab_obj_t *free_obj = (slab_obj_t *)obj;
    free_obj->next = slab->free_list;
    slab->free_list = free_obj;
    slab->in_use--;
}

/*
 * Find which slab an object belongs to
 */
static slab_t *find_slab_for_obj(kmem_cache_t *cache, void *obj) {
    uint64_t addr = (uint64_t)obj;

    /* Check partial slabs first (most likely) */
    for (slab_t *slab = cache->slabs_partial; slab; slab = slab->next) {
        uint64_t slab_start = (uint64_t)slab;
        uint64_t slab_end = slab_start + cache->slab_size * PAGE_SIZE;
        if (addr >= slab_start && addr < slab_end) {
            return slab;
        }
    }

    /* Check full slabs */
    for (slab_t *slab = cache->slabs_full; slab; slab = slab->next) {
        uint64_t slab_start = (uint64_t)slab;
        uint64_t slab_end = slab_start + cache->slab_size * PAGE_SIZE;
        if (addr >= slab_start && addr < slab_end) {
            return slab;
        }
    }

    return NULL;
}

/*
 * Allocate an object from a cache
 */
void *kmem_cache_alloc(kmem_cache_t *cache) {
    if (!cache) return NULL;

    slab_t *slab = NULL;
    void *obj = NULL;

    /* Try partial slabs first */
    if (cache->slabs_partial) {
        slab = cache->slabs_partial;
        obj = slab_alloc_obj(slab);

        /* Move to full list if now full */
        if (slab->in_use == slab->total) {
            slab_list_remove(&cache->slabs_partial, slab);
            slab_list_add(&cache->slabs_full, slab);
        }
    }
    /* Try empty slabs */
    else if (cache->slabs_empty) {
        slab = cache->slabs_empty;
        slab_list_remove(&cache->slabs_empty, slab);
        obj = slab_alloc_obj(slab);

        /* Move to appropriate list */
        if (slab->in_use == slab->total) {
            slab_list_add(&cache->slabs_full, slab);
        } else {
            slab_list_add(&cache->slabs_partial, slab);
        }
    }
    /* Need to create a new slab */
    else {
        slab = slab_create(cache);
        if (!slab) {
            if (cache->flags & SLAB_PANIC) {
                panic("kmem_cache_alloc: out of memory for cache '%s'", cache->name);
            }
            return NULL;
        }

        obj = slab_alloc_obj(slab);

        /* Move to appropriate list */
        if (slab->in_use == slab->total) {
            slab_list_add(&cache->slabs_full, slab);
        } else {
            slab_list_add(&cache->slabs_partial, slab);
        }
    }

    cache->alloc_count++;

    /* Zero if requested */
    if (obj && (cache->flags & SLAB_ZERO)) {
        uint8_t *p = (uint8_t *)obj;
        for (size_t i = 0; i < cache->obj_size; i++) {
            p[i] = 0;
        }
    }

    return obj;
}

/*
 * Free an object back to its cache
 */
void kmem_cache_free(kmem_cache_t *cache, void *obj) {
    if (!cache || !obj) return;

    /* Find the slab this object belongs to */
    slab_t *slab = find_slab_for_obj(cache, obj);
    if (!slab) {
        ERROR("kmem_cache_free: object %p not found in cache '%s'", obj, cache->name);
        return;
    }

    bool was_full = (slab->in_use == slab->total);

    slab_free_obj(slab, obj);
    cache->free_count++;

    /* Update slab list */
    if (was_full) {
        /* Was full, now partial */
        slab_list_remove(&cache->slabs_full, slab);
        slab_list_add(&cache->slabs_partial, slab);
    } else if (slab->in_use == 0) {
        /* Now empty */
        slab_list_remove(&cache->slabs_partial, slab);

        /* Keep one empty slab cached, destroy others */
        if (cache->slabs_empty == NULL) {
            slab_list_add(&cache->slabs_empty, slab);
        } else {
            slab_destroy(cache, slab);
        }
    }
}

/*
 * Find the appropriate kmalloc cache for a size
 */
static int find_kmalloc_cache(size_t size) {
    for (int i = 0; i < KMALLOC_MAX_CACHE; i++) {
        if (kmalloc_sizes[i] >= size) {
            return i;
        }
    }
    return -1;
}

/*
 * General-purpose allocation
 */
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* For very large allocations, use page allocator directly */
    if (size > SLAB_MAX_SIZE) {
        size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        void *ptr = heap_alloc_pages(pages);
        /* Store size at start for kfree - wasteful but simple */
        if (ptr) {
            /* We need to track the size somehow. For now, just return the pointer.
             * Large allocations will need special handling in kfree. */
            TRACE("kmalloc: large allocation %llu bytes at %p", (unsigned long long)size, ptr);
        }
        return ptr;
    }

    int idx = find_kmalloc_cache(size);
    if (idx < 0) {
        ERROR("kmalloc: no cache for size %llu", (unsigned long long)size);
        return NULL;
    }

    return kmem_cache_alloc(kmalloc_caches[idx]);
}

/*
 * Allocate zeroed memory
 */
void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        uint8_t *p = (uint8_t *)ptr;
        for (size_t i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

/*
 * Free memory allocated by kmalloc
 */
void kfree(void *ptr) {
    if (!ptr) return;

    uint64_t addr = (uint64_t)ptr;

    /* Check if it's in kernel heap range */
    if (addr < KHEAP_START || addr >= heap_current) {
        ERROR("kfree: pointer %p outside heap range", ptr);
        return;
    }

    /* Find which cache this belongs to */
    for (int i = 0; i < KMALLOC_MAX_CACHE; i++) {
        kmem_cache_t *cache = kmalloc_caches[i];
        if (!cache) continue;

        slab_t *slab = find_slab_for_obj(cache, ptr);
        if (slab) {
            kmem_cache_free(cache, ptr);
            return;
        }
    }

    /* Not found in any cache - might be a large allocation */
    WARN("kfree: pointer %p not found in any cache (may be large alloc)", ptr);
}

/*
 * Get the usable size of an allocation
 */
size_t ksize(void *ptr) {
    if (!ptr) return 0;

    uint64_t addr = (uint64_t)ptr;

    if (addr < KHEAP_START || addr >= heap_current) {
        return 0;
    }

    /* Find which cache this belongs to */
    for (int i = 0; i < KMALLOC_MAX_CACHE; i++) {
        kmem_cache_t *cache = kmalloc_caches[i];
        if (!cache) continue;

        slab_t *slab = find_slab_for_obj(cache, ptr);
        if (slab) {
            return cache->obj_size;
        }
    }

    return 0;
}

/*
 * Debug: dump cache statistics
 */
void kmem_cache_dump(kmem_cache_t *cache) {
    if (!cache) return;

    uint32_t full_count = 0, partial_count = 0, empty_count = 0;

    for (slab_t *s = cache->slabs_full; s; s = s->next) full_count++;
    for (slab_t *s = cache->slabs_partial; s; s = s->next) partial_count++;
    for (slab_t *s = cache->slabs_empty; s; s = s->next) empty_count++;

    kprintf("  %-16s  size=%5llu  objs/slab=%3u  slabs=%u/%u/%u (f/p/e)  allocs=%llu  frees=%llu\n",
            cache->name,
            (unsigned long long)cache->obj_size,
            cache->objs_per_slab,
            full_count, partial_count, empty_count,
            cache->alloc_count,
            cache->free_count);
}

/*
 * Debug: dump all cache statistics
 */
void slab_dump_stats(void) {
    kprintf("\n=== Slab Allocator Statistics ===\n");
    kprintf("  Heap: 0x%llx - 0x%llx (used: %llu KB)\n",
            (unsigned long long)KHEAP_START, (unsigned long long)heap_current,
            (unsigned long long)((heap_current - KHEAP_START) / 1024));
    kprintf("\n");

    for (kmem_cache_t *cache = cache_list; cache; cache = cache->next) {
        kmem_cache_dump(cache);
    }
}
