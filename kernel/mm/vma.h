/* vma.h - Virtual Memory Area Management */
#ifndef _KERNEL_MM_VMA_H
#define _KERNEL_MM_VMA_H

#include <stdint.h>
#include <stdbool.h>
#include "vmm.h"

/*
 * VMA flags
 */
#define VMA_READ        (1 << 0)    /* Readable */
#define VMA_WRITE       (1 << 1)    /* Writable */
#define VMA_EXEC        (1 << 2)    /* Executable */
#define VMA_SHARED      (1 << 3)    /* Shared mapping (not COW) */
#define VMA_ANONYMOUS   (1 << 4)    /* Anonymous mapping (no file backing) */
#define VMA_STACK       (1 << 5)    /* Stack region (grows down) */
#define VMA_HEAP        (1 << 6)    /* Heap region (grows up) */
#define VMA_FIXED       (1 << 7)    /* Fixed address mapping */

/*
 * Virtual Memory Area
 *
 * Represents a contiguous region of virtual memory with uniform properties.
 * Used for demand paging - pages are only allocated when first accessed.
 */
typedef struct vma {
    uint64_t start;             /* Start virtual address (page-aligned) */
    uint64_t end;               /* End virtual address (exclusive, page-aligned) */
    uint32_t flags;             /* VMA_* flags */
    uint32_t prot;              /* Protection flags (for PTE) */
    struct vma *next;           /* Next VMA in list (sorted by address) */
    struct vma *prev;           /* Previous VMA in list */

    /* For file-backed mappings (future) */
    void *file;                 /* File backing (NULL for anonymous) */
    uint64_t file_offset;       /* Offset in file */
} vma_t;

/*
 * VMA list head for an address space
 */
typedef struct vma_list {
    vma_t *head;                /* First VMA */
    vma_t *tail;                /* Last VMA */
    uint32_t count;             /* Number of VMAs */
} vma_list_t;

/*
 * Initialize VMA subsystem
 */
void vma_init(void);

/*
 * Create a new VMA list (for a new address space)
 */
vma_list_t *vma_list_create(void);

/*
 * Destroy a VMA list and all its VMAs
 */
void vma_list_destroy(vma_list_t *list);

/*
 * Clone a VMA list (for fork)
 */
vma_list_t *vma_list_clone(vma_list_t *src);

/*
 * Create and add a VMA to the list
 *
 * @param list   VMA list
 * @param start  Start address
 * @param end    End address (exclusive)
 * @param flags  VMA flags
 * @param prot   Protection flags (PTE flags)
 * @return New VMA, or NULL on failure
 */
vma_t *vma_create(vma_list_t *list, uint64_t start, uint64_t end,
                  uint32_t flags, uint32_t prot);

/*
 * Remove and free a VMA
 */
void vma_destroy(vma_list_t *list, vma_t *vma);

/*
 * Find VMA containing the given address
 *
 * @param list  VMA list
 * @param addr  Address to find
 * @return VMA containing addr, or NULL if not found
 */
vma_t *vma_find(vma_list_t *list, uint64_t addr);

/*
 * Find VMA for address or the next VMA after it
 *
 * @param list  VMA list
 * @param addr  Address to find
 * @return VMA containing or after addr, or NULL if none
 */
vma_t *vma_find_or_next(vma_list_t *list, uint64_t addr);

/*
 * Check if a range overlaps with any existing VMA
 *
 * @param list   VMA list
 * @param start  Start address
 * @param end    End address (exclusive)
 * @return true if overlaps, false otherwise
 */
bool vma_overlaps(vma_list_t *list, uint64_t start, uint64_t end);

/*
 * Find a free region of given size
 *
 * @param list   VMA list
 * @param size   Size needed (will be page-aligned)
 * @param hint   Hint address (or 0 for any)
 * @return Start address of free region, or 0 if none found
 */
uint64_t vma_find_free(vma_list_t *list, uint64_t size, uint64_t hint);

/*
 * Dump all VMAs (for debugging)
 */
void vma_dump(vma_list_t *list);

#endif /* _KERNEL_MM_VMA_H */
