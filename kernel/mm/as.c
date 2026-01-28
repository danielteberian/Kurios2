/* as.c - Address Space Management Implementation */

#include "as.h"
#include "pmm.h"
#include "slab.h"
#include "vmm.h"
#include "debug/debug.h"
#include "include/types.h"

/*
 * Kernel address space (initialized on first call)
 * This represents the kernel's page tables that all processes share
 */
static address_space_t kernel_as;
static bool kernel_as_initialized = false;

/*
 * PML4 index for start of kernel space (entry 256 = 0xFFFF800000000000)
 * All entries from 256-511 are kernel space and should be shared
 */
#define KERNEL_PML4_START   256

/*
 * Convert physical address to virtual for page table access
 * Same logic as in vmm.c
 */
static inline void* phys_to_virt(uint64_t phys) {
    if (phys >= KERNEL_PHYS_BASE && phys < KERNEL_PHYS_BASE + 0x8000000) {
        return (void*)KERNEL_PHYS_TO_VIRT(phys);
    }
    return (void*)phys;
}

/*
 * Convert page table virtual address to physical
 */
static uint64_t table_to_phys(page_table_t table) {
    uint64_t virt = (uint64_t)table;
    if (virt >= KERNEL_VIRT_BASE) {
        return KERNEL_VIRT_TO_PHYS(virt);
    }
    return virt;
}

/*
 * Allocate a zeroed page table
 */
static page_table_t alloc_page_table(void) {
    uint64_t phys = alloc_page();
    if (phys == 0) {
        ERROR("Failed to allocate page table");
        return NULL;
    }

    page_table_t table = (page_table_t)phys_to_virt(phys);
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        table[i] = 0;
    }

    return table;
}

/*
 * Free a page table
 */
static void free_page_table(page_table_t table) {
    uint64_t phys = table_to_phys(table);
    free_page(phys);
}

/*
 * Get page table from entry
 */
static page_table_t pte_to_table(pte_t entry) {
    if (!(entry & PTE_PRESENT)) {
        return NULL;
    }
    return (page_table_t)phys_to_virt(entry & PTE_ADDR_MASK);
}

/*
 * Get the kernel's address space
 */
address_space_t *as_get_kernel(void) {
    if (!kernel_as_initialized) {
        kernel_as.cr3 = vmm_get_cr3();
        kernel_as.ref_count = 1;
        kernel_as.user_pages = 0;
        kernel_as_initialized = true;
    }
    return &kernel_as;
}

/*
 * Create a new address space
 */
address_space_t *as_create(void) {
    /* Ensure kernel AS is initialized */
    as_get_kernel();

    /* Allocate address space structure */
    address_space_t *as = kmalloc(sizeof(address_space_t));
    if (!as) {
        ERROR("Failed to allocate address space structure");
        return NULL;
    }

    /* Allocate new PML4 */
    page_table_t new_pml4 = alloc_page_table();
    if (!new_pml4) {
        kfree(as);
        return NULL;
    }

    /* Get kernel's PML4 */
    page_table_t kernel_pml4 = (page_table_t)phys_to_virt(kernel_as.cr3 & PTE_ADDR_MASK);

    /* Copy kernel higher-half entries (256-511) to new PML4 */
    for (int i = KERNEL_PML4_START; i < ENTRIES_PER_TABLE; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    /* User space entries (0-255) are already zeroed */

    as->cr3 = table_to_phys(new_pml4);
    as->ref_count = 1;
    as->user_pages = 0;

    DEBUG("Created address space: cr3=0x%llx", as->cr3);

    return as;
}

/*
 * Recursively free page tables for user space
 */
static void free_page_tables_recursive(page_table_t table, int level) {
    if (!table || level < 0) return;

    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (!(table[i] & PTE_PRESENT)) continue;

        /* Check for huge pages - don't recurse */
        if ((level == 2 || level == 1) && (table[i] & PTE_HUGE)) {
            /* Free the huge page */
            free_page(table[i] & PTE_ADDR_MASK);
            continue;
        }

        if (level > 0) {
            /* Recurse to lower level */
            page_table_t child = pte_to_table(table[i]);
            free_page_tables_recursive(child, level - 1);
        } else {
            /* Level 0 (PT): free the actual page */
            free_page(table[i] & PTE_ADDR_MASK);
        }
    }

    /* Free this table itself */
    free_page_table(table);
}

/*
 * Destroy an address space
 */
void as_destroy(address_space_t *as) {
    if (!as) return;

    /* Don't destroy kernel address space */
    if (as == &kernel_as) {
        WARN("Attempt to destroy kernel address space");
        return;
    }

    as->ref_count--;
    if (as->ref_count > 0) {
        DEBUG("Address space ref_count decremented to %u", as->ref_count);
        return;
    }

    DEBUG("Destroying address space: cr3=0x%llx, user_pages=%llu",
          as->cr3, as->user_pages);

    /* Get PML4 */
    page_table_t pml4 = (page_table_t)phys_to_virt(as->cr3 & PTE_ADDR_MASK);

    /* Free user-space page tables (entries 0-255) */
    for (int i = 0; i < KERNEL_PML4_START; i++) {
        if (pml4[i] & PTE_PRESENT) {
            page_table_t pdpt = pte_to_table(pml4[i]);
            free_page_tables_recursive(pdpt, 2);  /* PDPT is level 2 */
        }
    }

    /* Free the PML4 itself */
    free_page(as->cr3 & PTE_ADDR_MASK);

    /* Free the structure */
    kfree(as);
}

/*
 * Switch to an address space
 */
void as_switch(address_space_t *as) {
    if (!as) {
        WARN("as_switch called with NULL");
        return;
    }

    uint64_t current_cr3 = vmm_get_cr3();
    if (current_cr3 != as->cr3) {
        vmm_set_cr3(as->cr3);
    }
}

/*
 * Get or create PTE in a specific address space
 */
static pte_t* as_get_pte(address_space_t *as, uint64_t virt, bool create) {
    page_table_t pml4 = (page_table_t)phys_to_virt(as->cr3 & PTE_ADDR_MASK);

    /* Get PML4 entry */
    uint64_t pml4_idx = PML4_INDEX(virt);
    pte_t* pml4e = &pml4[pml4_idx];

    /* Get or create PDPT */
    page_table_t pdpt;
    if (*pml4e & PTE_PRESENT) {
        pdpt = pte_to_table(*pml4e);
    } else if (create) {
        pdpt = alloc_page_table();
        if (!pdpt) return NULL;
        *pml4e = table_to_phys(pdpt) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else {
        return NULL;
    }

    /* Get PDPT entry */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    pte_t* pdpte = &pdpt[pdpt_idx];

    /* Check for 1GB huge page */
    if ((*pdpte & PTE_PRESENT) && (*pdpte & PTE_HUGE)) {
        return NULL;
    }

    /* Get or create PD */
    page_table_t pd;
    if (*pdpte & PTE_PRESENT) {
        pd = pte_to_table(*pdpte);
    } else if (create) {
        pd = alloc_page_table();
        if (!pd) return NULL;
        *pdpte = table_to_phys(pd) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else {
        return NULL;
    }

    /* Get PD entry */
    uint64_t pd_idx = PD_INDEX(virt);
    pte_t* pde = &pd[pd_idx];

    /* Check for 2MB huge page */
    if ((*pde & PTE_PRESENT) && (*pde & PTE_HUGE)) {
        return NULL;
    }

    /* Get or create PT */
    page_table_t pt;
    if (*pde & PTE_PRESENT) {
        pt = pte_to_table(*pde);
    } else if (create) {
        pt = alloc_page_table();
        if (!pt) return NULL;
        *pde = table_to_phys(pt) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    } else {
        return NULL;
    }

    /* Get PT entry */
    uint64_t pt_idx = PT_INDEX(virt);
    return &pt[pt_idx];
}

/*
 * Map a page in an address space
 */
int as_map_page(address_space_t *as, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!as) return -1;

    /* Align addresses */
    virt &= ~0xFFFUL;
    phys &= ~0xFFFUL;

    /* Get PTE, creating tables as needed */
    pte_t* pte = as_get_pte(as, virt, true);
    if (!pte) {
        ERROR("Failed to get PTE for 0x%llx in address space", virt);
        return -1;
    }

    /* Check if already mapped */
    if (*pte & PTE_PRESENT) {
        WARN("Page 0x%llx already mapped in address space", virt);
    }

    /* Set the mapping */
    *pte = (phys & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK) | PTE_PRESENT;

    /* Flush TLB if this is the current address space */
    if (as->cr3 == vmm_get_cr3()) {
        vmm_flush_page(virt);
    }

    return 0;
}

/*
 * Unmap a page from an address space
 */
uint64_t as_unmap_page(address_space_t *as, uint64_t virt) {
    if (!as) return 0;

    virt &= ~0xFFFUL;

    pte_t* pte = as_get_pte(as, virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return 0;  /* Not mapped */
    }

    uint64_t phys = *pte & PTE_ADDR_MASK;
    *pte = 0;

    /* Flush TLB if this is the current address space */
    if (as->cr3 == vmm_get_cr3()) {
        vmm_flush_page(virt);
    }

    return phys;
}

/*
 * Get physical address for a virtual address (handles huge pages)
 */
uint64_t as_get_phys(address_space_t *as, uint64_t virt) {
    if (!as) return 0;

    page_table_t pml4 = (page_table_t)phys_to_virt(as->cr3 & PTE_ADDR_MASK);

    /* Walk PML4 */
    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) return 0;
    page_table_t pdpt = pte_to_table(pml4[pml4_idx]);

    /* Walk PDPT */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return 0;
    /* Check for 1GB huge page */
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        return (pdpt[pdpt_idx] & 0xFFFFFFC0000000UL) | (virt & 0x3FFFFFFFUL);
    }
    page_table_t pd = pte_to_table(pdpt[pdpt_idx]);

    /* Walk PD */
    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT)) return 0;
    /* Check for 2MB huge page */
    if (pd[pd_idx] & PTE_HUGE) {
        return (pd[pd_idx] & 0xFFFFFFFE00000UL) | (virt & 0x1FFFFFUL);
    }
    page_table_t pt = pte_to_table(pd[pd_idx]);

    /* Walk PT */
    uint64_t pt_idx = PT_INDEX(virt);
    if (!(pt[pt_idx] & PTE_PRESENT)) return 0;
    return (pt[pt_idx] & PTE_ADDR_MASK) | PAGE_OFFSET(virt);
}

/*
 * Check if a virtual address is mapped (handles huge pages)
 */
bool as_is_mapped(address_space_t *as, uint64_t virt) {
    if (!as) return false;

    page_table_t pml4 = (page_table_t)phys_to_virt(as->cr3 & PTE_ADDR_MASK);

    /* Walk PML4 */
    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) return false;
    page_table_t pdpt = pte_to_table(pml4[pml4_idx]);

    /* Walk PDPT */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return false;
    /* Check for 1GB huge page */
    if (pdpt[pdpt_idx] & PTE_HUGE) return true;
    page_table_t pd = pte_to_table(pdpt[pdpt_idx]);

    /* Walk PD */
    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT)) return false;
    /* Check for 2MB huge page */
    if (pd[pd_idx] & PTE_HUGE) return true;
    page_table_t pt = pte_to_table(pd[pd_idx]);

    /* Walk PT */
    uint64_t pt_idx = PT_INDEX(virt);
    return (pt[pt_idx] & PTE_PRESENT) != 0;
}

/*
 * Allocate and map a user page
 */
int as_alloc_page(address_space_t *as, uint64_t virt, uint64_t flags) {
    if (!as) return -1;

    /* Verify it's user space */
    if (!as_is_user_addr(virt)) {
        ERROR("as_alloc_page: address 0x%llx is not in user space", virt);
        return -1;
    }

    /* Allocate physical page */
    uint64_t phys = alloc_page();
    if (phys == 0) {
        ERROR("as_alloc_page: failed to allocate physical page");
        return -1;
    }

    /* Zero the page */
    void *page = phys_to_virt(phys);
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        ((uint64_t*)page)[i] = 0;
    }

    /* Map with PTE_USER flag */
    if (as_map_page(as, virt, phys, flags | PTE_USER) != 0) {
        free_page(phys);
        return -1;
    }

    as->user_pages++;
    return 0;
}

/*
 * Allocate and map multiple user pages
 */
int as_alloc_pages(address_space_t *as, uint64_t virt, uint64_t count, uint64_t flags) {
    for (uint64_t i = 0; i < count; i++) {
        if (as_alloc_page(as, virt + i * PAGE_SIZE, flags) != 0) {
            /* Rollback: free what we've allocated */
            for (uint64_t j = 0; j < i; j++) {
                as_free_page(as, virt + j * PAGE_SIZE);
            }
            return -1;
        }
    }
    return 0;
}

/*
 * Free and unmap a user page
 */
void as_free_page(address_space_t *as, uint64_t virt) {
    if (!as) return;

    uint64_t phys = as_unmap_page(as, virt);
    if (phys) {
        free_page(phys);
        if (as->user_pages > 0) {
            as->user_pages--;
        }
    }
}

/*
 * Clone an address space (for fork)
 */
address_space_t *as_clone(address_space_t *src) {
    if (!src) return NULL;

    /* Create new address space (gets kernel mappings) */
    address_space_t *dst = as_create();
    if (!dst) return NULL;

    /* Get source PML4 */
    page_table_t src_pml4 = (page_table_t)phys_to_virt(src->cr3 & PTE_ADDR_MASK);

    /* Clone user space mappings (PML4 entries 0-255) */
    for (int pml4_i = 0; pml4_i < KERNEL_PML4_START; pml4_i++) {
        if (!(src_pml4[pml4_i] & PTE_PRESENT)) continue;

        page_table_t src_pdpt = pte_to_table(src_pml4[pml4_i]);

        for (int pdpt_i = 0; pdpt_i < ENTRIES_PER_TABLE; pdpt_i++) {
            if (!(src_pdpt[pdpt_i] & PTE_PRESENT)) continue;

            /* Handle 1GB huge page */
            if (src_pdpt[pdpt_i] & PTE_HUGE) {
                /* TODO: copy huge page */
                WARN("as_clone: 1GB huge pages not yet supported");
                continue;
            }

            page_table_t src_pd = pte_to_table(src_pdpt[pdpt_i]);

            for (int pd_i = 0; pd_i < ENTRIES_PER_TABLE; pd_i++) {
                if (!(src_pd[pd_i] & PTE_PRESENT)) continue;

                /* Handle 2MB huge page */
                if (src_pd[pd_i] & PTE_HUGE) {
                    /* TODO: copy huge page */
                    WARN("as_clone: 2MB huge pages not yet supported");
                    continue;
                }

                page_table_t src_pt = pte_to_table(src_pd[pd_i]);

                for (int pt_i = 0; pt_i < ENTRIES_PER_TABLE; pt_i++) {
                    if (!(src_pt[pt_i] & PTE_PRESENT)) continue;

                    /* Calculate virtual address */
                    uint64_t virt = ((uint64_t)pml4_i << 39) |
                                    ((uint64_t)pdpt_i << 30) |
                                    ((uint64_t)pd_i << 21) |
                                    ((uint64_t)pt_i << 12);

                    /* Get source physical address and flags */
                    uint64_t src_phys = src_pt[pt_i] & PTE_ADDR_MASK;
                    uint64_t flags = src_pt[pt_i] & ~PTE_ADDR_MASK;

                    /* Allocate new page */
                    uint64_t dst_phys = alloc_page();
                    if (!dst_phys) {
                        ERROR("as_clone: failed to allocate page");
                        as_destroy(dst);
                        return NULL;
                    }

                    /* Copy page contents */
                    void *src_page = phys_to_virt(src_phys);
                    void *dst_page = phys_to_virt(dst_phys);
                    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
                        ((uint64_t*)dst_page)[i] = ((uint64_t*)src_page)[i];
                    }

                    /* Map in destination */
                    if (as_map_page(dst, virt, dst_phys, flags) != 0) {
                        free_page(dst_phys);
                        as_destroy(dst);
                        return NULL;
                    }

                    dst->user_pages++;
                }
            }
        }
    }

    DEBUG("Cloned address space: src cr3=0x%llx, dst cr3=0x%llx, pages=%llu",
          src->cr3, dst->cr3, dst->user_pages);

    return dst;
}

#ifdef DEBUG_TESTS
/*
 * Address space tests
 */
void as_run_tests(void) {
    kprintf("\n=== Address Space Tests ===\n");

    /* Test 1: Kernel address space */
    address_space_t *kernel = as_get_kernel();
    kprintf("  Test 1 - Kernel AS: %s (cr3=0x%llx)\n",
            kernel ? "OK" : "FAIL", kernel ? kernel->cr3 : 0);

    /* Test 2: Create new address space */
    address_space_t *as = as_create();
    kprintf("  Test 2 - Create AS: %s (cr3=0x%llx)\n",
            as ? "OK" : "FAIL", as ? as->cr3 : 0);
    if (!as) return;

    /* Test 3: Kernel mappings are present */
    uint64_t kernel_test_addr = KERNEL_VIRT_BASE;
    bool has_kernel = as_is_mapped(as, kernel_test_addr);
    kprintf("  Test 3 - Kernel mapping present: %s\n",
            has_kernel ? "OK" : "FAIL");

    /* Test 4: User space is empty */
    uint64_t user_test_addr = 0x400000;
    bool has_user = as_is_mapped(as, user_test_addr);
    kprintf("  Test 4 - User space empty: %s\n",
            !has_user ? "OK" : "FAIL");

    /* Test 5: Allocate user page */
    int ret = as_alloc_page(as, user_test_addr, PTE_WRITABLE);
    kprintf("  Test 5 - Alloc user page: %s\n", ret == 0 ? "OK" : "FAIL");

    /* Test 6: Verify mapping exists */
    has_user = as_is_mapped(as, user_test_addr);
    kprintf("  Test 6 - Mapping exists: %s\n", has_user ? "OK" : "FAIL");

    /* Test 7: Write to user page */
    /* Switch to AS, write, switch back */
    address_space_t *saved = as_get_kernel();
    as_switch(as);

    volatile uint64_t *user_ptr = (volatile uint64_t *)user_test_addr;
    *user_ptr = 0xDEADBEEFCAFEBABEUL;
    uint64_t read_val = *user_ptr;

    as_switch(saved);

    kprintf("  Test 7 - Write/read user page: %s (0x%llx)\n",
            read_val == 0xDEADBEEFCAFEBABEUL ? "OK" : "FAIL", read_val);

    /* Test 8: Get physical address */
    uint64_t phys = as_get_phys(as, user_test_addr);
    kprintf("  Test 8 - Get phys: %s (0x%llx)\n",
            phys != 0 ? "OK" : "FAIL", phys);

    /* Test 9: Clone address space */
    address_space_t *clone = as_clone(as);
    kprintf("  Test 9 - Clone AS: %s\n", clone ? "OK" : "FAIL");

    if (clone) {
        /* Verify cloned page has same value */
        as_switch(clone);
        volatile uint64_t *clone_ptr = (volatile uint64_t *)user_test_addr;
        uint64_t clone_val = *clone_ptr;
        as_switch(saved);

        kprintf("  Test 10 - Clone data: %s (0x%llx)\n",
                clone_val == 0xDEADBEEFCAFEBABEUL ? "OK" : "FAIL", clone_val);

        /* Destroy clone */
        as_destroy(clone);
        kprintf("  Test 11 - Destroy clone: OK\n");
    }

    /* Test 12: Free user page */
    as_free_page(as, user_test_addr);
    has_user = as_is_mapped(as, user_test_addr);
    kprintf("  Test 12 - Free user page: %s\n", !has_user ? "OK" : "FAIL");

    /* Test 13: user_pages counter */
    kprintf("  Test 13 - User pages count: %s (%llu)\n",
            as->user_pages == 0 ? "OK" : "FAIL", as->user_pages);

    /* Destroy address space */
    as_destroy(as);
    kprintf("  Test 14 - Destroy AS: OK\n");

    kprintf("\n  Address space tests complete.\n");
}
#endif
