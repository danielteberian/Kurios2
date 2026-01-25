/* vmm.c - Virtual Memory Manager Implementation */

#include "vmm.h"
#include "pmm.h"
#include "debug/debug.h"
#include "include/types.h"

/*
 * Convert physical address to virtual (for accessing page tables)
 * We use the identity mapping in low memory and higher-half for kernel space
 */
static inline void* phys_to_virt(uint64_t phys) {
    /* If it's in the kernel-mapped region, use higher-half */
    if (phys >= KERNEL_PHYS_BASE && phys < KERNEL_PHYS_BASE + 0x8000000) {
        return (void*)KERNEL_PHYS_TO_VIRT(phys);
    }
    /* Otherwise use identity mapping (first 4GB is identity mapped) */
    return (void*)phys;
}

/*
 * Get the current PML4 table
 */
static page_table_t get_pml4(void) {
    uint64_t cr3 = vmm_get_cr3();
    return (page_table_t)phys_to_virt(cr3 & PTE_ADDR_MASK);
}

/*
 * Allocate a new page table (zeroed)
 */
static page_table_t alloc_page_table(void) {
    uint64_t phys = alloc_page();
    if (phys == 0) {
        ERROR("Failed to allocate page table");
        return NULL;
    }

    /* Zero the page table */
    page_table_t table = (page_table_t)phys_to_virt(phys);
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        table[i] = 0;
    }

    return table;
}

/*
 * Get virtual address of a page table from its entry
 */
static page_table_t pte_to_table(pte_t entry) {
    if (!(entry & PTE_PRESENT)) {
        return NULL;
    }
    return (page_table_t)phys_to_virt(entry & PTE_ADDR_MASK);
}

/*
 * Get physical address of a page table
 */
static uint64_t table_to_phys(page_table_t table) {
    uint64_t virt = (uint64_t)table;

    /* Check if it's in higher-half */
    if (virt >= KERNEL_VIRT_BASE) {
        return KERNEL_VIRT_TO_PHYS(virt);
    }
    /* Identity mapped */
    return virt;
}

/*
 * Initialize VMM
 */
void vmm_init(void) {
    INFO("Initializing Virtual Memory Manager");

    /* We inherit the page tables from the bootloader */
    uint64_t cr3 = vmm_get_cr3();
    DEBUG("Current CR3: 0x%llx", cr3);

    /* Verify we can access the page tables */
    page_table_t pml4 = get_pml4();
    DEBUG("PML4 at virtual: 0x%llx", (uint64_t)pml4);

    /* Count mapped entries */
    int mapped = 0;
    for (int i = 0; i < ENTRIES_PER_TABLE; i++) {
        if (pml4[i] & PTE_PRESENT) {
            mapped++;
        }
    }
    DEBUG("PML4 has %d present entries", mapped);

    INFO("VMM initialized");
}

/*
 * Get or create a page table entry
 */
pte_t* vmm_get_pte(uint64_t virt, bool create) {
    page_table_t pml4 = get_pml4();

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
        *pml4e = table_to_phys(pdpt) | PTE_PRESENT | PTE_WRITABLE;
    } else {
        return NULL;
    }

    /* Get PDPT entry */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    pte_t* pdpte = &pdpt[pdpt_idx];

    /* Check for 1GB huge page */
    if ((*pdpte & PTE_PRESENT) && (*pdpte & PTE_HUGE)) {
        /* Can't get PTE for huge page */
        return NULL;
    }

    /* Get or create PD */
    page_table_t pd;
    if (*pdpte & PTE_PRESENT) {
        pd = pte_to_table(*pdpte);
    } else if (create) {
        pd = alloc_page_table();
        if (!pd) return NULL;
        *pdpte = table_to_phys(pd) | PTE_PRESENT | PTE_WRITABLE;
    } else {
        return NULL;
    }

    /* Get PD entry */
    uint64_t pd_idx = PD_INDEX(virt);
    pte_t* pde = &pd[pd_idx];

    /* Check for 2MB huge page */
    if ((*pde & PTE_PRESENT) && (*pde & PTE_HUGE)) {
        /* Can't get PTE for huge page */
        return NULL;
    }

    /* Get or create PT */
    page_table_t pt;
    if (*pde & PTE_PRESENT) {
        pt = pte_to_table(*pde);
    } else if (create) {
        pt = alloc_page_table();
        if (!pt) return NULL;
        *pde = table_to_phys(pt) | PTE_PRESENT | PTE_WRITABLE;
    } else {
        return NULL;
    }

    /* Get PT entry */
    uint64_t pt_idx = PT_INDEX(virt);
    return &pt[pt_idx];
}

/*
 * Map a single 4KB page
 */
int vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    /* Align addresses */
    virt &= ~0xFFFUL;
    phys &= ~0xFFFUL;

    /* Get PTE, creating tables as needed */
    pte_t* pte = vmm_get_pte(virt, true);
    if (!pte) {
        ERROR("Failed to get PTE for 0x%llx", virt);
        return -1;
    }

    /* Check if already mapped */
    if (*pte & PTE_PRESENT) {
        WARN("Page 0x%llx already mapped to 0x%llx", virt, *pte & PTE_ADDR_MASK);
    }

    /* Set the mapping */
    *pte = (phys & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK) | PTE_PRESENT;

    /* Flush TLB for this page */
    vmm_flush_page(virt);

    return 0;
}

/*
 * Map multiple contiguous pages
 */
int vmm_map_pages(uint64_t virt, uint64_t phys, uint64_t count, uint64_t flags) {
    for (uint64_t i = 0; i < count; i++) {
        if (vmm_map_page(virt + i * PAGE_SIZE_4K,
                         phys + i * PAGE_SIZE_4K,
                         flags) != 0) {
            /* Unmap what we've mapped so far */
            vmm_unmap_pages(virt, i);
            return -1;
        }
    }
    return 0;
}

/*
 * Unmap a single page
 */
uint64_t vmm_unmap_page(uint64_t virt) {
    virt &= ~0xFFFUL;

    pte_t* pte = vmm_get_pte(virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return 0;  /* Not mapped */
    }

    uint64_t phys = *pte & PTE_ADDR_MASK;
    *pte = 0;

    vmm_flush_page(virt);

    return phys;
}

/*
 * Unmap multiple pages
 */
void vmm_unmap_pages(uint64_t virt, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        vmm_unmap_page(virt + i * PAGE_SIZE_4K);
    }
}

/*
 * Get physical address for a virtual address
 * Handles 4KB pages, 2MB pages, and 1GB pages
 */
uint64_t vmm_get_phys(uint64_t virt) {
    page_table_t pml4 = get_pml4();

    /* Walk PML4 */
    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) {
        return 0;
    }
    page_table_t pdpt = pte_to_table(pml4[pml4_idx]);

    /* Walk PDPT */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
        return 0;
    }
    /* Check for 1GB huge page */
    if (pdpt[pdpt_idx] & PTE_HUGE) {
        return (pdpt[pdpt_idx] & 0xFFFFFFC0000000UL) | (virt & 0x3FFFFFFFUL);
    }
    page_table_t pd = pte_to_table(pdpt[pdpt_idx]);

    /* Walk PD */
    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT)) {
        return 0;
    }
    /* Check for 2MB huge page */
    if (pd[pd_idx] & PTE_HUGE) {
        return (pd[pd_idx] & 0xFFFFFFFE00000UL) | (virt & 0x1FFFFFUL);
    }
    page_table_t pt = pte_to_table(pd[pd_idx]);

    /* Walk PT */
    uint64_t pt_idx = PT_INDEX(virt);
    if (!(pt[pt_idx] & PTE_PRESENT)) {
        return 0;
    }
    return (pt[pt_idx] & PTE_ADDR_MASK) | PAGE_OFFSET(virt);
}

/*
 * Check if a virtual address is mapped (internal, returns page size or 0)
 */
static uint64_t vmm_get_mapping_size(uint64_t virt) {
    page_table_t pml4 = get_pml4();

    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) return 0;
    page_table_t pdpt = pte_to_table(pml4[pml4_idx]);

    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return 0;
    if (pdpt[pdpt_idx] & PTE_HUGE) return PAGE_SIZE_1G;
    page_table_t pd = pte_to_table(pdpt[pdpt_idx]);

    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT)) return 0;
    if (pd[pd_idx] & PTE_HUGE) return PAGE_SIZE_2M;
    page_table_t pt = pte_to_table(pd[pd_idx]);

    uint64_t pt_idx = PT_INDEX(virt);
    if (!(pt[pt_idx] & PTE_PRESENT)) return 0;
    return PAGE_SIZE_4K;
}

/*
 * Check if a virtual address is mapped
 */
bool vmm_is_mapped(uint64_t virt) {
    return vmm_get_mapping_size(virt) != 0;
}

/*
 * Debug: dump page table entry flags
 */
static void dump_flags(uint64_t flags) {
    if (flags & PTE_PRESENT)  kprintf("P");  else kprintf("-");
    if (flags & PTE_WRITABLE) kprintf("W");  else kprintf("R");
    if (flags & PTE_USER)     kprintf("U");  else kprintf("K");
    if (flags & PTE_PWT)      kprintf("T");  else kprintf("-");
    if (flags & PTE_PCD)      kprintf("C");  else kprintf("-");
    if (flags & PTE_ACCESSED) kprintf("A");  else kprintf("-");
    if (flags & PTE_DIRTY)    kprintf("D");  else kprintf("-");
    if (flags & PTE_HUGE)     kprintf("H");  else kprintf("-");
    if (flags & PTE_GLOBAL)   kprintf("G");  else kprintf("-");
    if (flags & PTE_NX)       kprintf("X");  else kprintf("-");
}

/*
 * Debug: dump PTE for an address
 */
void vmm_dump_pte(uint64_t virt) {
    kprintf("PTE for 0x%016llx:\n", virt);
    kprintf("  PML4[%llu] PDPT[%llu] PD[%llu] PT[%llu]\n",
            PML4_INDEX(virt), PDPT_INDEX(virt),
            PD_INDEX(virt), PT_INDEX(virt));

    pte_t* pte = vmm_get_pte(virt, false);
    if (!pte) {
        kprintf("  Not mapped (no page table)\n");
        return;
    }

    if (!(*pte & PTE_PRESENT)) {
        kprintf("  Not present (PTE=0x%016llx)\n", *pte);
        return;
    }

    kprintf("  Physical: 0x%016llx\n", *pte & PTE_ADDR_MASK);
    kprintf("  Flags:    ");
    dump_flags(*pte);
    kprintf(" (0x%llx)\n", *pte & 0xFFF);
}

/*
 * Debug: dump mappings in a range
 */
void vmm_dump_mappings(uint64_t start, uint64_t end) {
    kprintf("\n=== VMM Mappings 0x%llx - 0x%llx ===\n", start, end);

    uint64_t virt = start & ~0xFFFUL;
    uint64_t mapped_start = 0;
    uint64_t mapped_phys = 0;
    uint64_t mapped_flags = 0;
    bool in_run = false;

    while (virt < end) {
        pte_t* pte = vmm_get_pte(virt, false);
        bool present = pte && (*pte & PTE_PRESENT);

        if (present) {
            uint64_t phys = *pte & PTE_ADDR_MASK;
            uint64_t flags = *pte & 0xFFF;

            if (!in_run) {
                /* Start new run */
                mapped_start = virt;
                mapped_phys = phys;
                mapped_flags = flags;
                in_run = true;
            } else if (phys != mapped_phys + (virt - mapped_start) ||
                       flags != mapped_flags) {
                /* End current run, print it */
                kprintf("  0x%016llx - 0x%016llx -> 0x%016llx ",
                        mapped_start, virt, mapped_phys);
                dump_flags(mapped_flags);
                kprintf("\n");

                /* Start new run */
                mapped_start = virt;
                mapped_phys = phys;
                mapped_flags = flags;
            }
        } else if (in_run) {
            /* End current run */
            kprintf("  0x%016llx - 0x%016llx -> 0x%016llx ",
                    mapped_start, virt, mapped_phys);
            dump_flags(mapped_flags);
            kprintf("\n");
            in_run = false;
        }

        virt += PAGE_SIZE_4K;
    }

    if (in_run) {
        kprintf("  0x%016llx - 0x%016llx -> 0x%016llx ",
                mapped_start, virt, mapped_phys);
        dump_flags(mapped_flags);
        kprintf("\n");
    }
}
