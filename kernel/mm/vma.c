/* vma.c - Virtual Memory Area Implementation */

#include "vma.h"
#include "slab.h"
#include "pmm.h"
#include "debug/debug.h"

/* Slab cache for VMA structures */
static kmem_cache_t *vma_cache;
static kmem_cache_t *vma_list_cache;

/*
 * Initialize VMA subsystem
 */
void vma_init(void) {
    INFO("Initializing VMA subsystem");

    vma_cache = kmem_cache_create("vma", sizeof(vma_t), 0, 0);
    if (!vma_cache) {
        panic("Failed to create VMA slab cache");
    }

    vma_list_cache = kmem_cache_create("vma_list", sizeof(vma_list_t), 0, 0);
    if (!vma_list_cache) {
        panic("Failed to create VMA list slab cache");
    }

    INFO("VMA subsystem initialized");
}

/*
 * Create a new VMA list
 */
vma_list_t *vma_list_create(void) {
    vma_list_t *list = kmem_cache_alloc(vma_list_cache);
    if (!list) {
        ERROR("Failed to allocate VMA list");
        return NULL;
    }

    list->head = NULL;
    list->tail = NULL;
    list->count = 0;

    return list;
}

/*
 * Destroy a VMA list and all its VMAs
 */
void vma_list_destroy(vma_list_t *list) {
    if (!list) return;

    vma_t *vma = list->head;
    while (vma) {
        vma_t *next = vma->next;
        kmem_cache_free(vma_cache, vma);
        vma = next;
    }

    kmem_cache_free(vma_list_cache, list);
}

/*
 * Clone a VMA list (for fork)
 */
vma_list_t *vma_list_clone(vma_list_t *src) {
    if (!src) return NULL;

    vma_list_t *dst = vma_list_create();
    if (!dst) return NULL;

    for (vma_t *vma = src->head; vma; vma = vma->next) {
        vma_t *new_vma = vma_create(dst, vma->start, vma->end, vma->flags, vma->prot);
        if (!new_vma) {
            vma_list_destroy(dst);
            return NULL;
        }
        new_vma->file = vma->file;
        new_vma->file_offset = vma->file_offset;
    }

    return dst;
}

/*
 * Insert VMA into list (sorted by address)
 */
static void vma_insert(vma_list_t *list, vma_t *vma) {
    vma_t *prev = NULL;
    vma_t *curr = list->head;

    /* Find insertion point */
    while (curr && curr->start < vma->start) {
        prev = curr;
        curr = curr->next;
    }

    /* Insert */
    vma->prev = prev;
    vma->next = curr;

    if (prev) {
        prev->next = vma;
    } else {
        list->head = vma;
    }

    if (curr) {
        curr->prev = vma;
    } else {
        list->tail = vma;
    }

    list->count++;
}

/*
 * Create and add a VMA to the list
 */
vma_t *vma_create(vma_list_t *list, uint64_t start, uint64_t end,
                  uint32_t flags, uint32_t prot) {
    if (!list) return NULL;

    /* Align addresses */
    start = ALIGN_DOWN(start, PAGE_SIZE);
    end = ALIGN_UP(end, PAGE_SIZE);

    if (start >= end) {
        ERROR("vma_create: invalid range 0x%llx - 0x%llx", start, end);
        return NULL;
    }

    /* Check for overlaps */
    if (vma_overlaps(list, start, end)) {
        ERROR("vma_create: range 0x%llx - 0x%llx overlaps existing VMA", start, end);
        return NULL;
    }

    /* Allocate VMA */
    vma_t *vma = kmem_cache_alloc(vma_cache);
    if (!vma) {
        ERROR("Failed to allocate VMA");
        return NULL;
    }

    vma->start = start;
    vma->end = end;
    vma->flags = flags;
    vma->prot = prot;
    vma->next = NULL;
    vma->prev = NULL;
    vma->file = NULL;
    vma->file_offset = 0;

    /* Insert into list */
    vma_insert(list, vma);

    DEBUG("Created VMA: 0x%llx - 0x%llx, flags=0x%x, prot=0x%x",
          start, end, flags, prot);

    return vma;
}

/*
 * Remove and free a VMA
 */
void vma_destroy(vma_list_t *list, vma_t *vma) {
    if (!list || !vma) return;

    /* Remove from list */
    if (vma->prev) {
        vma->prev->next = vma->next;
    } else {
        list->head = vma->next;
    }

    if (vma->next) {
        vma->next->prev = vma->prev;
    } else {
        list->tail = vma->prev;
    }

    list->count--;

    /* Free VMA */
    kmem_cache_free(vma_cache, vma);
}

/*
 * Find VMA containing the given address
 */
vma_t *vma_find(vma_list_t *list, uint64_t addr) {
    if (!list) return NULL;

    for (vma_t *vma = list->head; vma; vma = vma->next) {
        if (addr >= vma->start && addr < vma->end) {
            return vma;
        }
        if (vma->start > addr) {
            break;  /* Past the address, won't find it */
        }
    }

    return NULL;
}

/*
 * Find VMA for address or the next VMA after it
 */
vma_t *vma_find_or_next(vma_list_t *list, uint64_t addr) {
    if (!list) return NULL;

    for (vma_t *vma = list->head; vma; vma = vma->next) {
        if (addr < vma->end) {
            return vma;
        }
    }

    return NULL;
}

/*
 * Check if a range overlaps with any existing VMA
 */
bool vma_overlaps(vma_list_t *list, uint64_t start, uint64_t end) {
    if (!list) return false;

    for (vma_t *vma = list->head; vma; vma = vma->next) {
        /* Check for overlap: !(end <= vma->start || start >= vma->end) */
        if (start < vma->end && end > vma->start) {
            return true;
        }
        if (vma->start >= end) {
            break;  /* Past the range, no more overlaps possible */
        }
    }

    return false;
}

/*
 * Find a free region of given size
 */
uint64_t vma_find_free(vma_list_t *list, uint64_t size, uint64_t hint) {
    if (!list) return 0;

    /* Align size */
    size = ALIGN_UP(size, PAGE_SIZE);

    /* User space bounds */
    uint64_t space_start = 0x400000;  /* Start after first 4MB */
    uint64_t space_end = 0x7FFFFFFFFFFF;

    /* Try hint first */
    if (hint) {
        hint = ALIGN_UP(hint, PAGE_SIZE);
        if (hint >= space_start && hint + size <= space_end) {
            if (!vma_overlaps(list, hint, hint + size)) {
                return hint;
            }
        }
    }

    /* Search for a gap */
    uint64_t search_start = space_start;

    for (vma_t *vma = list->head; vma; vma = vma->next) {
        /* Check gap before this VMA */
        if (search_start + size <= vma->start) {
            return search_start;
        }
        /* Move past this VMA */
        if (vma->end > search_start) {
            search_start = vma->end;
        }
    }

    /* Check space after last VMA */
    if (search_start + size <= space_end) {
        return search_start;
    }

    return 0;  /* No space found */
}

/*
 * Dump all VMAs (for debugging)
 */
void vma_dump(vma_list_t *list) {
    if (!list) {
        kprintf("VMA list: (null)\n");
        return;
    }

    kprintf("VMA list (%u entries):\n", list->count);
    for (vma_t *vma = list->head; vma; vma = vma->next) {
        kprintf("  0x%012llx - 0x%012llx  %c%c%c  flags=0x%x\n",
                vma->start, vma->end,
                (vma->flags & VMA_READ)  ? 'r' : '-',
                (vma->flags & VMA_WRITE) ? 'w' : '-',
                (vma->flags & VMA_EXEC)  ? 'x' : '-',
                vma->flags);
    }
}
