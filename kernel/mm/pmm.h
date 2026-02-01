/* pmm.h - Physical Memory Manager (Buddy Allocator) */
#ifndef _KERNEL_MM_PMM_H
#define _KERNEL_MM_PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Buddy Allocator Configuration
 *
 * MAX_ORDER: Maximum allocation order (2^MAX_ORDER pages = 4MB with order 10)
 * This matches Linux's default MAX_ORDER of 11 (but we use 10 for 4MB max)
 */
#define MAX_ORDER       10
#define PAGE_SIZE       4096UL
#define PAGE_SHIFT      12

/* Page flags */
#define PAGE_FLAG_BUDDY     (1 << 0)    /* Part of buddy system */
#define PAGE_FLAG_RESERVED  (1 << 1)    /* Reserved (not usable) */
#define PAGE_FLAG_KERNEL    (1 << 2)    /* Kernel code/data */
#define PAGE_FLAG_HEAD      (1 << 3)    /* First page of a compound allocation */
#define PAGE_FLAG_TAIL      (1 << 4)    /* Tail page of a compound allocation */
#define PAGE_FLAG_COW       (1 << 5)    /* Copy-on-write page (shared, needs copy on write) */

/*
 * Page descriptor - one per physical page
 * Kept small to minimize memory overhead
 */
typedef struct page {
    struct page *next;      /* Free list linkage */
    struct page *prev;
    uint32_t flags;         /* Page flags */
    uint32_t order;         /* Allocation order (if head of compound page) */
    uint32_t refcount;      /* Reference count */
    uint32_t _reserved;     /* Padding for alignment */
} page_t;

/*
 * Memory zone - manages a contiguous region of physical memory
 * For now we just have one zone (all usable RAM)
 */
typedef struct zone {
    page_t *free_lists[MAX_ORDER + 1];  /* Free lists per order */
    uint64_t free_count[MAX_ORDER + 1]; /* Count of free blocks per order */
    uint64_t base_pfn;                  /* Starting page frame number */
    uint64_t page_count;                /* Total pages in zone */
    uint64_t free_pages;                /* Total free pages */
} zone_t;

/*
 * Global memory information
 */
typedef struct mem_info {
    uint64_t total_pages;       /* Total physical pages */
    uint64_t free_pages;        /* Currently free pages */
    uint64_t reserved_pages;    /* Reserved/unusable pages */
    uint64_t kernel_pages;      /* Pages used by kernel */
    page_t *page_array;         /* Array of page descriptors */
    uint64_t page_array_pages;  /* Pages used by page_array itself */
    zone_t zone;                /* Single zone for now */
} mem_info_t;

extern mem_info_t mem_info;

/* Initialize PMM from E820 memory map */
void pmm_init(void *boot_info);

/* Allocate 2^order contiguous pages, returns physical address or 0 on failure */
uint64_t alloc_pages(unsigned int order);

/* Free 2^order contiguous pages starting at physical address */
void free_pages(uint64_t phys_addr, unsigned int order);

/* Allocate a single page */
static inline uint64_t alloc_page(void) {
    return alloc_pages(0);
}

/* Free a single page */
static inline void free_page(uint64_t phys_addr) {
    free_pages(phys_addr, 0);
}

/* Convert between physical address and page frame number */
static inline uint64_t phys_to_pfn(uint64_t phys) {
    return phys >> PAGE_SHIFT;
}

static inline uint64_t pfn_to_phys(uint64_t pfn) {
    return pfn << PAGE_SHIFT;
}

/* Get page descriptor for a physical address */
page_t *phys_to_page(uint64_t phys_addr);

/* Get physical address from page descriptor */
uint64_t page_to_phys(page_t *page);

/*
 * Reference counting for COW pages
 */

/* Increment page reference count */
void page_get(page_t *page);

/* Decrement page reference count, returns new count */
uint32_t page_put(page_t *page);

/* Get current reference count */
static inline uint32_t page_refcount(page_t *page) {
    return page ? page->refcount : 0;
}

/* Increment refcount for physical address */
void page_get_phys(uint64_t phys);

/* Decrement refcount for physical address, free if zero, returns new count */
uint32_t page_put_phys(uint64_t phys);

/* Debug: print PMM statistics */
void pmm_dump_stats(void);

/* Debug: print free lists */
void pmm_dump_free_lists(void);

#endif /* _KERNEL_MM_PMM_H */
