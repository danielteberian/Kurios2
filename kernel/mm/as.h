/* as.h - Address Space Management */
#ifndef _KERNEL_MM_AS_H
#define _KERNEL_MM_AS_H

#include <stdint.h>
#include <stdbool.h>
#include "vmm.h"
#include "vma.h"

/*
 * Address Space structure
 *
 * Represents a process's virtual address space. The kernel higher-half
 * (PML4 entries 256-511) is shared across all address spaces, while
 * the user lower-half (PML4 entries 0-255) is per-process.
 */
typedef struct address_space {
    uint64_t cr3;           /* Physical address of PML4 */
    uint32_t ref_count;     /* Reference count (for shared mappings) */
    uint64_t user_pages;    /* Number of user-space pages allocated */
    vma_list_t *vmas;       /* Virtual memory areas (for demand paging) */
} address_space_t;

/*
 * Get the kernel's address space
 * This is the address space used by the kernel and inherited by all processes
 */
address_space_t *as_get_kernel(void);

/*
 * Create a new address space
 * Allocates a new PML4 and copies kernel higher-half mappings
 *
 * @return New address space, or NULL on failure
 */
address_space_t *as_create(void);

/*
 * Destroy an address space
 * Frees all user-space pages and page tables, but NOT kernel mappings
 *
 * @param as Address space to destroy
 */
void as_destroy(address_space_t *as);

/*
 * Switch to an address space
 * Sets CR3 to the address space's page tables
 *
 * @param as Address space to switch to
 */
void as_switch(address_space_t *as);

/*
 * Clone an address space (for fork)
 * Creates a new address space with copies of all user-space mappings
 *
 * @param src Source address space to clone
 * @return New cloned address space, or NULL on failure
 */
address_space_t *as_clone(address_space_t *src);

/*
 * Map a page in an address space
 *
 * @param as    Address space to map in
 * @param virt  Virtual address (page-aligned)
 * @param phys  Physical address (page-aligned)
 * @param flags Page table entry flags
 * @return 0 on success, -1 on failure
 */
int as_map_page(address_space_t *as, uint64_t virt, uint64_t phys, uint64_t flags);

/*
 * Unmap a page from an address space
 *
 * @param as   Address space to unmap from
 * @param virt Virtual address (page-aligned)
 * @return Physical address that was mapped, or 0 if not mapped
 */
uint64_t as_unmap_page(address_space_t *as, uint64_t virt);

/*
 * Get physical address for a virtual address in an address space
 *
 * @param as   Address space to look up in
 * @param virt Virtual address
 * @return Physical address, or 0 if not mapped
 */
uint64_t as_get_phys(address_space_t *as, uint64_t virt);

/*
 * Check if a virtual address is mapped in an address space
 *
 * @param as   Address space to check
 * @param virt Virtual address
 * @return true if mapped, false otherwise
 */
bool as_is_mapped(address_space_t *as, uint64_t virt);

/*
 * Allocate and map a user page
 * Allocates physical memory and maps it at the given virtual address
 *
 * @param as    Address space
 * @param virt  Virtual address (must be in user space)
 * @param flags Page flags (PTE_USER will be added automatically)
 * @return 0 on success, -1 on failure
 */
int as_alloc_page(address_space_t *as, uint64_t virt, uint64_t flags);

/*
 * Allocate and map multiple user pages
 *
 * @param as    Address space
 * @param virt  Starting virtual address
 * @param count Number of pages
 * @param flags Page flags
 * @return 0 on success, -1 on failure
 */
int as_alloc_pages(address_space_t *as, uint64_t virt, uint64_t count, uint64_t flags);

/*
 * Free and unmap a user page
 *
 * @param as   Address space
 * @param virt Virtual address
 */
void as_free_page(address_space_t *as, uint64_t virt);

/*
 * Check if an address is in user space
 *
 * @param virt Virtual address to check
 * @return true if in user space (lower half), false otherwise
 */
static inline bool as_is_user_addr(uint64_t virt) {
    /* User space is the lower half: 0x0000000000000000 - 0x00007FFFFFFFFFFF */
    return virt < 0x0000800000000000UL;
}

/*
 * Check if an address is in kernel space
 *
 * @param virt Virtual address to check
 * @return true if in kernel space (higher half), false otherwise
 */
static inline bool as_is_kernel_addr(uint64_t virt) {
    /* Kernel space is the higher half: 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF */
    return virt >= 0xFFFF800000000000UL;
}

/*
 * User space address range constants
 */
#define USER_SPACE_START    0x0000000000400000UL   /* Start of user code (1MB) */
#define USER_SPACE_END      0x00007FFFFFFFF000UL   /* End of user space */
#define USER_STACK_TOP      0x00007FFFFFF00000UL   /* Top of user stack region */
#define USER_STACK_SIZE     (16 * PAGE_SIZE)        /* Default stack size: 64KB */

#ifdef DEBUG_TESTS
/*
 * Run address space tests
 */
void as_run_tests(void);
#endif

#endif /* _KERNEL_MM_AS_H */
