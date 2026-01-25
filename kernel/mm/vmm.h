/* vmm.h - Virtual Memory Manager */
#ifndef _KERNEL_MM_VMM_H
#define _KERNEL_MM_VMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "include/types.h"

/*
 * x86_64 Page Table Entry Flags
 */
#define PTE_PRESENT     (1UL << 0)   /* Page is present in memory */
#define PTE_WRITABLE    (1UL << 1)   /* Page is writable */
#define PTE_USER        (1UL << 2)   /* Page is accessible from user mode */
#define PTE_PWT         (1UL << 3)   /* Page-level write-through */
#define PTE_PCD         (1UL << 4)   /* Page-level cache disable */
#define PTE_ACCESSED    (1UL << 5)   /* Page has been accessed */
#define PTE_DIRTY       (1UL << 6)   /* Page has been written to */
#define PTE_HUGE        (1UL << 7)   /* Huge page (2MB in PD, 1GB in PDPT) */
#define PTE_GLOBAL      (1UL << 8)   /* Global page (not flushed on CR3 switch) */
#define PTE_NX          (1UL << 63)  /* No-execute (requires EFER.NXE) */

/* Common flag combinations */
#define PTE_KERNEL_RW   (PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL)
#define PTE_KERNEL_RO   (PTE_PRESENT | PTE_GLOBAL)
#define PTE_KERNEL_RWX  (PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL)
#define PTE_KERNEL_RX   (PTE_PRESENT | PTE_GLOBAL)
#define PTE_USER_RW     (PTE_PRESENT | PTE_WRITABLE | PTE_USER)
#define PTE_USER_RO     (PTE_PRESENT | PTE_USER)
#define PTE_USER_RWX    (PTE_PRESENT | PTE_WRITABLE | PTE_USER)
#define PTE_USER_RX     (PTE_PRESENT | PTE_USER)

/* Address mask for page table entries (bits 12-51) */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000UL

/* Page table index extraction macros */
#define PML4_INDEX(addr)    (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)    (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)      (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)      (((addr) >> 12) & 0x1FF)
#define PAGE_OFFSET(addr)   ((addr) & 0xFFF)

/* Number of entries per page table */
#define ENTRIES_PER_TABLE   512

/* Page sizes - use PAGE_SIZE from types.h, define 4K alias */
#define PAGE_SIZE_4K    PAGE_SIZE

/*
 * Kernel virtual address space layout:
 *
 * 0xFFFFFFFF80000000 - 0xFFFFFFFF87FFFFFF : Kernel image (128MB)
 * 0xFFFFFFFF88000000 - 0xFFFFFFFFFFFFFFFF : Kernel heap/dynamic (2GB - 128MB)
 *
 * Lower half (0x0000000000000000 - 0x00007FFFFFFFFFFF) : User space
 */
#define KERNEL_VIRT_BASE    0xFFFFFFFF80000000UL
#define KERNEL_PHYS_BASE    0x200000UL

/* Convert between kernel virtual and physical addresses */
#define KERNEL_PHYS_TO_VIRT(phys) ((phys) - KERNEL_PHYS_BASE + KERNEL_VIRT_BASE)
#define KERNEL_VIRT_TO_PHYS(virt) ((virt) - KERNEL_VIRT_BASE + KERNEL_PHYS_BASE)

/* Page table type (just a uint64_t array) */
typedef uint64_t pte_t;
typedef pte_t* page_table_t;

/*
 * Initialize VMM
 * Must be called after PMM is initialized
 */
void vmm_init(void);

/*
 * Map a single 4KB page
 * Returns 0 on success, -1 on failure
 */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);

/*
 * Map multiple contiguous pages
 * Returns 0 on success, -1 on failure
 */
int vmm_map_pages(uint64_t virt, uint64_t phys, uint64_t count, uint64_t flags);

/*
 * Unmap a single 4KB page
 * Returns the physical address that was mapped, or 0 if not mapped
 */
uint64_t vmm_unmap_page(uint64_t virt);

/*
 * Unmap multiple pages
 */
void vmm_unmap_pages(uint64_t virt, uint64_t count);

/*
 * Get the physical address for a virtual address
 * Returns 0 if not mapped
 */
uint64_t vmm_get_phys(uint64_t virt);

/*
 * Check if a virtual address is mapped
 */
bool vmm_is_mapped(uint64_t virt);

/*
 * Get or create a page table entry
 * If create is true, allocates intermediate tables as needed
 * Returns pointer to PTE, or NULL on failure
 */
pte_t* vmm_get_pte(uint64_t virt, bool create);

/*
 * Flush TLB for a single page
 */
static inline void vmm_flush_page(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/*
 * Flush entire TLB
 */
static inline void vmm_flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/*
 * Get current page table root (CR3)
 */
static inline uint64_t vmm_get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/*
 * Set page table root (CR3)
 */
static inline void vmm_set_cr3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

/*
 * Debug: dump page table entry
 */
void vmm_dump_pte(uint64_t virt);

/*
 * Debug: dump mappings in a range
 */
void vmm_dump_mappings(uint64_t start, uint64_t end);

#endif /* _KERNEL_MM_VMM_H */
