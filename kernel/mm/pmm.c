/* pmm.c - Physical Memory Manager (Buddy Allocator) Implementation */

#include "pmm.h"
#include "debug/debug.h"
#include "boot_info.h"
#include "include/types.h"

/* Global memory info */
mem_info_t mem_info;

/* Kernel boundaries (from linker script) */
extern uint64_t _kernel_phys_start;
extern uint64_t _kernel_phys_end;

/*
 * Helper: Check if two blocks are buddies
 * Buddies have addresses that differ only in bit 'order'
 */
static inline uint64_t get_buddy_pfn(uint64_t pfn, unsigned int order) {
    return pfn ^ (1UL << order);
}

/*
 * Helper: Add page to free list
 */
static void free_list_add(zone_t *zone, page_t *page, unsigned int order) {
    page_t *head = zone->free_lists[order];

    page->prev = NULL;
    page->next = head;
    page->order = order;
    page->flags |= PAGE_FLAG_BUDDY;

    if (head) {
        head->prev = page;
    }
    zone->free_lists[order] = page;
    zone->free_count[order]++;
}

/*
 * Helper: Remove page from free list
 */
static void free_list_remove(zone_t *zone, page_t *page, unsigned int order) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        zone->free_lists[order] = page->next;
    }

    if (page->next) {
        page->next->prev = page->prev;
    }

    page->prev = NULL;
    page->next = NULL;
    page->flags &= ~PAGE_FLAG_BUDDY;
    zone->free_count[order]--;
}

/*
 * Initialize PMM from boot info
 */
void pmm_init(void *boot_info_ptr) {
    BootInfo *boot_info = (BootInfo *)boot_info_ptr;
    MemoryMapEntry *mmap = (MemoryMapEntry *)(uintptr_t)boot_info->memory_map;
    uint64_t mmap_count = boot_info->memory_count;

    INFO("Initializing Physical Memory Manager (Buddy Allocator)");

    /* First pass: find total memory and highest address */
    uint64_t max_phys_addr = 0;
    uint64_t total_usable = 0;

    for (uint64_t i = 0; i < mmap_count; i++) {
        uint64_t end = mmap[i].base + mmap[i].length;
        if (end > max_phys_addr) {
            max_phys_addr = end;
        }
        if (mmap[i].type == MMAP_TYPE_USABLE) {
            total_usable += mmap[i].length;
        }
    }

    /* Calculate total pages and page array size */
    uint64_t total_pages = phys_to_pfn(ALIGN_UP(max_phys_addr, PAGE_SIZE));
    uint64_t page_array_size = total_pages * sizeof(page_t);
    uint64_t page_array_pages = ALIGN_UP(page_array_size, PAGE_SIZE) / PAGE_SIZE;

    DEBUG("Max physical address: 0x%llx", max_phys_addr);
    DEBUG("Total pages: %llu", total_pages);
    DEBUG("Page array size: %llu bytes (%llu pages)", page_array_size, page_array_pages);

    /*
     * Place page array after kernel
     * Kernel is at physical 0x200000, find where it ends
     */
    uint64_t kernel_phys_end = (uint64_t)&_kernel_phys_end;
    uint64_t page_array_phys = ALIGN_UP(kernel_phys_end, PAGE_SIZE);

    /* Convert to virtual address (higher half) */
    uint64_t page_array_virt = page_array_phys + 0xFFFFFFFF80000000UL - 0x200000;

    DEBUG("Page array at physical 0x%llx, virtual 0x%llx", page_array_phys, page_array_virt);

    /* Initialize mem_info */
    mem_info.total_pages = total_pages;
    mem_info.free_pages = 0;
    mem_info.reserved_pages = 0;
    mem_info.kernel_pages = 0;
    mem_info.page_array = (page_t *)page_array_virt;
    mem_info.page_array_pages = page_array_pages;

    /* Initialize zone */
    zone_t *zone = &mem_info.zone;
    zone->base_pfn = 0;
    zone->page_count = total_pages;
    zone->free_pages = 0;

    for (int i = 0; i <= MAX_ORDER; i++) {
        zone->free_lists[i] = NULL;
        zone->free_count[i] = 0;
    }

    /* Zero out page array */
    page_t *pages = mem_info.page_array;
    for (uint64_t i = 0; i < total_pages; i++) {
        pages[i].next = NULL;
        pages[i].prev = NULL;
        pages[i].flags = PAGE_FLAG_RESERVED;  /* Mark all as reserved initially */
        pages[i].order = 0;
        pages[i].refcount = 0;
    }

    /*
     * Second pass: mark usable regions
     * Then add them to free lists
     */
    for (uint64_t i = 0; i < mmap_count; i++) {
        if (mmap[i].type != MMAP_TYPE_USABLE) {
            continue;
        }

        uint64_t base = ALIGN_UP(mmap[i].base, PAGE_SIZE);
        uint64_t end = ALIGN_DOWN(mmap[i].base + mmap[i].length, PAGE_SIZE);

        /* Skip low memory (first 1MB) - reserved for legacy/BIOS */
        if (base < 0x100000) {
            base = 0x100000;
        }

        if (end <= base) {
            continue;
        }

        DEBUG("Processing usable region: 0x%llx - 0x%llx", base, end);

        /* Mark pages as not reserved (but don't add to free list yet) */
        uint64_t start_pfn = phys_to_pfn(base);
        uint64_t end_pfn = phys_to_pfn(end);

        for (uint64_t pfn = start_pfn; pfn < end_pfn; pfn++) {
            pages[pfn].flags = 0;  /* Clear reserved flag */
        }
    }

    /*
     * Mark kernel pages as used
     */
    uint64_t kernel_start_pfn = phys_to_pfn(0x200000);  /* Kernel physical base */
    uint64_t kernel_end_pfn = phys_to_pfn(ALIGN_UP(kernel_phys_end, PAGE_SIZE));

    DEBUG("Kernel pages: PFN %llu - %llu", kernel_start_pfn, kernel_end_pfn);

    for (uint64_t pfn = kernel_start_pfn; pfn < kernel_end_pfn; pfn++) {
        pages[pfn].flags = PAGE_FLAG_KERNEL | PAGE_FLAG_RESERVED;
        mem_info.kernel_pages++;
    }

    /*
     * Mark page array pages as used
     */
    uint64_t pa_start_pfn = phys_to_pfn(page_array_phys);
    uint64_t pa_end_pfn = phys_to_pfn(ALIGN_UP(page_array_phys + page_array_size, PAGE_SIZE));

    DEBUG("Page array pages: PFN %llu - %llu", pa_start_pfn, pa_end_pfn);

    for (uint64_t pfn = pa_start_pfn; pfn < pa_end_pfn; pfn++) {
        pages[pfn].flags = PAGE_FLAG_KERNEL | PAGE_FLAG_RESERVED;
        mem_info.kernel_pages++;
    }

    /*
     * Third pass: add free pages to buddy allocator
     * We add pages in largest possible blocks for efficiency
     */
    for (uint64_t i = 0; i < mmap_count; i++) {
        if (mmap[i].type != MMAP_TYPE_USABLE) {
            continue;
        }

        uint64_t base = ALIGN_UP(mmap[i].base, PAGE_SIZE);
        uint64_t end = ALIGN_DOWN(mmap[i].base + mmap[i].length, PAGE_SIZE);

        if (base < 0x100000) {
            base = 0x100000;
        }

        if (end <= base) {
            continue;
        }

        uint64_t pfn = phys_to_pfn(base);
        uint64_t end_pfn = phys_to_pfn(end);

        while (pfn < end_pfn) {
            /* Skip reserved pages */
            if (pages[pfn].flags & PAGE_FLAG_RESERVED) {
                pfn++;
                continue;
            }

            /* Find the largest order we can use */
            unsigned int order;
            for (order = MAX_ORDER; order > 0; order--) {
                uint64_t block_pages = 1UL << order;
                uint64_t block_mask = block_pages - 1;

                /* Check alignment and size */
                if ((pfn & block_mask) == 0 && (pfn + block_pages) <= end_pfn) {
                    /* Check all pages in block are free */
                    bool all_free = true;
                    for (uint64_t j = 0; j < block_pages; j++) {
                        if (pages[pfn + j].flags & PAGE_FLAG_RESERVED) {
                            all_free = false;
                            break;
                        }
                    }
                    if (all_free) {
                        break;
                    }
                }
            }

            /* Add block to free list */
            uint64_t block_pages = 1UL << order;
            free_list_add(zone, &pages[pfn], order);
            zone->free_pages += block_pages;
            mem_info.free_pages += block_pages;

            pfn += block_pages;
        }
    }

    /* Count reserved pages */
    for (uint64_t pfn = 0; pfn < total_pages; pfn++) {
        if ((pages[pfn].flags & PAGE_FLAG_RESERVED) &&
            !(pages[pfn].flags & PAGE_FLAG_KERNEL)) {
            mem_info.reserved_pages++;
        }
    }

    INFO("PMM initialized: %llu MB total, %llu MB free, %llu MB reserved",
         (mem_info.total_pages * PAGE_SIZE) / (1024 * 1024),
         (mem_info.free_pages * PAGE_SIZE) / (1024 * 1024),
         (mem_info.reserved_pages * PAGE_SIZE) / (1024 * 1024));
}

/*
 * Allocate 2^order contiguous pages
 */
uint64_t alloc_pages(unsigned int order) {
    if (order > MAX_ORDER) {
        ERROR("alloc_pages: order %u exceeds MAX_ORDER %d", order, MAX_ORDER);
        return 0;
    }

    zone_t *zone = &mem_info.zone;
    page_t *pages = mem_info.page_array;

    /* Find a free block of sufficient size */
    unsigned int current_order;
    for (current_order = order; current_order <= MAX_ORDER; current_order++) {
        if (zone->free_lists[current_order] != NULL) {
            break;
        }
    }

    if (current_order > MAX_ORDER) {
        /* No free memory available */
        WARN("alloc_pages: out of memory (requested order %u)", order);
        return 0;
    }

    /* Remove block from free list */
    page_t *block = zone->free_lists[current_order];
    free_list_remove(zone, block, current_order);

    /* Split the block down to the requested size */
    while (current_order > order) {
        current_order--;
        uint64_t pfn = (block - pages);
        uint64_t buddy_pfn = pfn + (1UL << current_order);

        /* Add the upper buddy to the free list */
        free_list_add(zone, &pages[buddy_pfn], current_order);
    }

    /* Mark as allocated */
    uint64_t pfn = (block - pages);
    uint64_t num_pages = 1UL << order;

    block->flags |= PAGE_FLAG_HEAD;
    block->order = order;
    block->refcount = 1;

    for (uint64_t i = 1; i < num_pages; i++) {
        pages[pfn + i].flags |= PAGE_FLAG_TAIL;
        pages[pfn + i].refcount = 1;
    }

    zone->free_pages -= num_pages;
    mem_info.free_pages -= num_pages;

    return pfn_to_phys(pfn);
}

/*
 * Free 2^order contiguous pages
 */
void free_pages(uint64_t phys_addr, unsigned int order) {
    if (order > MAX_ORDER) {
        ERROR("free_pages: order %u exceeds MAX_ORDER %d", order, MAX_ORDER);
        return;
    }

    if (phys_addr == 0) {
        ERROR("free_pages: attempted to free NULL");
        return;
    }

    if (!IS_ALIGNED(phys_addr, PAGE_SIZE << order)) {
        ERROR("free_pages: address 0x%llx not aligned for order %u", phys_addr, order);
        return;
    }

    zone_t *zone = &mem_info.zone;
    page_t *pages = mem_info.page_array;
    uint64_t pfn = phys_to_pfn(phys_addr);
    uint64_t num_pages = 1UL << order;

    /* Clear allocation flags */
    pages[pfn].flags &= ~PAGE_FLAG_HEAD;
    pages[pfn].refcount = 0;

    for (uint64_t i = 1; i < num_pages; i++) {
        pages[pfn + i].flags &= ~PAGE_FLAG_TAIL;
        pages[pfn + i].refcount = 0;
    }

    /* Try to merge with buddy */
    while (order < MAX_ORDER) {
        uint64_t buddy_pfn = get_buddy_pfn(pfn, order);
        page_t *buddy = &pages[buddy_pfn];

        /* Check if buddy is free and same order */
        if (!(buddy->flags & PAGE_FLAG_BUDDY) || buddy->order != order) {
            break;
        }

        /* Remove buddy from its free list */
        free_list_remove(zone, buddy, order);

        /* Merge: use lower address as new block */
        if (buddy_pfn < pfn) {
            pfn = buddy_pfn;
        }

        order++;
    }

    /* Add merged block to free list */
    free_list_add(zone, &pages[pfn], order);
    zone->free_pages += num_pages;
    mem_info.free_pages += num_pages;
}

/*
 * Get page descriptor for physical address
 */
page_t *phys_to_page(uint64_t phys_addr) {
    uint64_t pfn = phys_to_pfn(phys_addr);
    if (pfn >= mem_info.total_pages) {
        return NULL;
    }
    return &mem_info.page_array[pfn];
}

/*
 * Get physical address from page descriptor
 */
uint64_t page_to_phys(page_t *page) {
    uint64_t pfn = page - mem_info.page_array;
    return pfn_to_phys(pfn);
}

/*
 * Debug: print PMM statistics
 */
void pmm_dump_stats(void) {
    kprintf("\n=== PMM Statistics ===\n");
    kprintf("  Total pages:    %llu (%llu MB)\n",
            mem_info.total_pages, (mem_info.total_pages * PAGE_SIZE) / (1024 * 1024));
    kprintf("  Free pages:     %llu (%llu MB)\n",
            mem_info.free_pages, (mem_info.free_pages * PAGE_SIZE) / (1024 * 1024));
    kprintf("  Reserved pages: %llu (%llu MB)\n",
            mem_info.reserved_pages, (mem_info.reserved_pages * PAGE_SIZE) / (1024 * 1024));
    kprintf("  Kernel pages:   %llu (%llu KB)\n",
            mem_info.kernel_pages, (mem_info.kernel_pages * PAGE_SIZE) / 1024);
    kprintf("  Page array:     %llu pages (%llu KB)\n",
            mem_info.page_array_pages, (mem_info.page_array_pages * PAGE_SIZE) / 1024);
}

/*
 * Debug: print free list contents
 */
void pmm_dump_free_lists(void) {
    zone_t *zone = &mem_info.zone;

    kprintf("\n=== Free Lists ===\n");
    for (int order = 0; order <= MAX_ORDER; order++) {
        uint64_t block_size = (PAGE_SIZE << order) / 1024;
        const char *unit = "KB";
        if (block_size >= 1024) {
            block_size /= 1024;
            unit = "MB";
        }
        kprintf("  Order %2d (%4llu %s): %llu blocks\n",
                order, block_size, unit, zone->free_count[order]);
    }
}
