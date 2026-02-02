/* fault.c - Page Fault Handler Implementation */

#include "fault.h"
#include "pmm.h"
#include "vmm.h"
#include "as.h"
#include "vma.h"
#include "page_cache.h"
#include "debug/debug.h"
#include "../process/process.h"
#include "../arch/x86_64/cpu.h"
#include "../lib/string.h"

/*
 * Convert physical address to virtual for page access
 */
static inline void* phys_to_virt(uint64_t phys) {
    if (phys >= KERNEL_PHYS_BASE && phys < KERNEL_PHYS_BASE + 0x8000000) {
        return (void*)KERNEL_PHYS_TO_VIRT(phys);
    }
    return (void*)phys;
}

/*
 * Get PTE for an address in the current address space
 */
static pte_t* get_pte_for_addr(uint64_t cr3, uint64_t virt) {
    page_table_t pml4 = (page_table_t)phys_to_virt(cr3 & PTE_ADDR_MASK);

    /* Walk PML4 */
    uint64_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PTE_PRESENT)) return NULL;
    page_table_t pdpt = (page_table_t)phys_to_virt(pml4[pml4_idx] & PTE_ADDR_MASK);

    /* Walk PDPT */
    uint64_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PTE_PRESENT)) return NULL;
    if (pdpt[pdpt_idx] & PTE_HUGE) return NULL;  /* 1GB page */
    page_table_t pd = (page_table_t)phys_to_virt(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    /* Walk PD */
    uint64_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PTE_PRESENT)) return NULL;
    if (pd[pd_idx] & PTE_HUGE) return NULL;  /* 2MB page */
    page_table_t pt = (page_table_t)phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);

    /* Return PT entry */
    uint64_t pt_idx = PT_INDEX(virt);
    return &pt[pt_idx];
}

/*
 * Handle a COW (Copy-on-Write) fault
 */
int handle_cow_fault(uint64_t fault_addr) {
    uint64_t page_addr = fault_addr & ~0xFFFUL;
    uint64_t cr3 = vmm_get_cr3();

    /* Get PTE */
    pte_t *pte = get_pte_for_addr(cr3, page_addr);
    if (!pte) {
        ERROR("COW fault: no PTE for 0x%llx", fault_addr);
        return -1;
    }

    /* Verify it's a COW page */
    if (!(*pte & PTE_PRESENT) || !(*pte & PTE_COW)) {
        ERROR("COW fault: page 0x%llx is not a COW page (pte=0x%llx)", fault_addr, *pte);
        return -1;
    }

    uint64_t old_phys = *pte & PTE_ADDR_MASK;
    page_t *old_page = phys_to_page(old_phys);

    if (!old_page) {
        ERROR("COW fault: invalid physical page for 0x%llx", fault_addr);
        return -1;
    }

    /* Check refcount - if we're the only reference, just make writable */
    if (old_page->refcount == 1) {
        DEBUG("COW: page 0x%llx has refcount 1, making writable", page_addr);
        *pte = (*pte | PTE_WRITABLE) & ~PTE_COW;
        vmm_flush_page(page_addr);
        return 0;
    }

    /* Multiple references - need to copy */
    DEBUG("COW: copying page 0x%llx (refcount=%u)", page_addr, old_page->refcount);

    /* Allocate new page */
    uint64_t new_phys = alloc_page();
    if (!new_phys) {
        ERROR("COW fault: failed to allocate new page");
        return -1;
    }

    /* Copy contents */
    void *old_data = phys_to_virt(old_phys);
    void *new_data = phys_to_virt(new_phys);
    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        ((uint64_t*)new_data)[i] = ((uint64_t*)old_data)[i];
    }

    /* Update PTE: new physical address, writable, not COW */
    uint64_t flags = (*pte & ~PTE_ADDR_MASK & ~PTE_COW) | PTE_WRITABLE;
    *pte = (new_phys & PTE_ADDR_MASK) | flags;

    /* Decrement old page refcount */
    page_put_phys(old_phys);

    /* Flush TLB */
    vmm_flush_page(page_addr);

    DEBUG("COW: page 0x%llx copied to new phys 0x%llx", page_addr, new_phys);
    return 0;
}

/*
 * Handle a demand page fault
 *
 * Called when a user process accesses an address that is in a valid VMA
 * but has no physical page mapped yet. Allocates a page and maps it.
 * For file-backed VMAs, reads data from the page cache.
 */
int handle_demand_fault(uint64_t fault_addr) {
    process_t *proc = process_current();
    if (!proc) {
        ERROR("Demand fault with no current process");
        return -1;
    }

    /* Check if process has VMA tracking */
    if (!proc->as || !proc->as->vmas) {
        DEBUG("Demand fault: no VMA tracking for process %u", proc->pid);
        return -1;
    }

    /* Find VMA containing the faulting address */
    vma_t *vma = vma_find(proc->as->vmas, fault_addr);
    if (!vma) {
        DEBUG("Demand fault: address 0x%llx not in any VMA", fault_addr);
        return -1;  /* Invalid access - not in any VMA */
    }

    /* Check if this VMA is readable (basic permission check) */
    if (!(vma->flags & VMA_READ)) {
        DEBUG("Demand fault: VMA at 0x%llx is not readable", fault_addr);
        return -1;
    }

    uint64_t page_addr = fault_addr & ~0xFFFUL;
    uint64_t phys;

    /* Check if this is a file-backed mapping */
    vfs_node_t *file = (vfs_node_t *)vma->file;
    if (file && !(vma->flags & VMA_ANONYMOUS)) {
        /* File-backed: get page from page cache */
        uint64_t file_offset = vma->file_offset + (page_addr - vma->start);

        phys = page_cache_get(file, file_offset);
        if (!phys) {
            ERROR("Demand fault: failed to get page from cache for 0x%llx", page_addr);
            return -1;
        }

        DEBUG("Demand paging (file): page 0x%llx from file offset 0x%llx",
              page_addr, file_offset);

        /* For MAP_PRIVATE file mappings, we need to use COW */
        if (!(vma->flags & VMA_SHARED) && (vma->flags & VMA_WRITE)) {
            /* MAP_PRIVATE + writable: set up COW so writes get a private copy */
            uint64_t pte_flags = PTE_PRESENT | PTE_USER | PTE_COW;
            if (!(vma->flags & VMA_EXEC)) pte_flags |= PTE_NX;

            if (as_map_page(proc->as, page_addr, phys, pte_flags) != 0) {
                ERROR("Demand fault: failed to map COW page at 0x%llx", page_addr);
                page_cache_put(file, file_offset);
                return -1;
            }
        } else {
            /* MAP_SHARED or read-only: map directly */
            uint64_t pte_flags = PTE_PRESENT | PTE_USER;
            if ((vma->flags & VMA_WRITE) && (vma->flags & VMA_SHARED)) {
                pte_flags |= PTE_WRITABLE;
            }
            if (!(vma->flags & VMA_EXEC)) pte_flags |= PTE_NX;

            if (as_map_page(proc->as, page_addr, phys, pte_flags) != 0) {
                ERROR("Demand fault: failed to map page at 0x%llx", page_addr);
                page_cache_put(file, file_offset);
                return -1;
            }
        }
    } else {
        /* Anonymous mapping: allocate a new zeroed page */
        phys = alloc_page();
        if (!phys) {
            ERROR("Demand fault: failed to allocate page for 0x%llx", page_addr);
            return -1;
        }

        /* Zero the page */
        void *page_data = phys_to_virt(phys);
        memset(page_data, 0, PAGE_SIZE);

        /* Build PTE flags from VMA */
        uint64_t pte_flags = PTE_PRESENT | PTE_USER;
        if (vma->flags & VMA_WRITE) pte_flags |= PTE_WRITABLE;
        if (!(vma->flags & VMA_EXEC)) pte_flags |= PTE_NX;

        /* Map the page in the process's address space */
        if (as_map_page(proc->as, page_addr, phys, pte_flags) != 0) {
            ERROR("Demand fault: failed to map page at 0x%llx", page_addr);
            free_page(phys);
            return -1;
        }
    }

    proc->as->user_pages++;

    DEBUG("Demand paging: mapped page at 0x%llx -> 0x%llx (VMA 0x%llx-0x%llx)",
          page_addr, phys, vma->start, vma->end);

    return 0;
}

/*
 * Page fault handler
 */
void page_fault_handler(cpu_state_t *state) {
    uint64_t fault_addr = read_cr2();
    uint64_t error = state->error_code;

    bool is_present = (error & PF_PRESENT) != 0;
    bool is_write = (error & PF_WRITE) != 0;
    bool is_user = (error & PF_USER) != 0;

    DEBUG("Page fault at 0x%llx: %s %s %s (error=0x%llx, rip=0x%llx)",
          fault_addr,
          is_present ? "protection" : "not-present",
          is_write ? "write" : "read",
          is_user ? "user" : "kernel",
          error, state->rip);

    /* Check for COW fault: present + write + has COW flag */
    if (is_present && is_write) {
        uint64_t cr3 = vmm_get_cr3();
        pte_t *pte = get_pte_for_addr(cr3, fault_addr);

        if (pte && (*pte & PTE_COW)) {
            if (handle_cow_fault(fault_addr) == 0) {
                return;  /* COW handled successfully */
            }
            /* COW failed - fall through to error */
        }
    }

    /* Check for demand paging: not present + user + in valid VMA */
    if (!is_present && is_user) {
        /* First, try demand paging via VMA */
        if (handle_demand_fault(fault_addr) == 0) {
            return;  /* Demand fault handled successfully */
        }

        /* Demand paging didn't handle it - try stack growth as fallback */
        process_t *proc = process_current();
        if (proc) {
            uint64_t stack_top = USER_STACK_TOP;
            uint64_t stack_bottom = stack_top - (256 * PAGE_SIZE);  /* 1MB max stack */

            if (fault_addr >= stack_bottom && fault_addr < stack_top) {
                /* This looks like stack growth - allocate the page */
                uint64_t page_addr = fault_addr & ~0xFFFUL;

                DEBUG("Stack growth: allocating page at 0x%llx", page_addr);

                uint64_t phys = alloc_page();
                if (phys) {
                    /* Zero the page */
                    void *data = phys_to_virt(phys);
                    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
                        ((uint64_t*)data)[i] = 0;
                    }

                    /* Map it */
                    pte_t *pte = vmm_get_pte(page_addr, true);
                    if (pte) {
                        *pte = (phys & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
                        vmm_flush_page(page_addr);
                        DEBUG("Stack page allocated at 0x%llx -> 0x%llx", page_addr, phys);
                        return;
                    }
                    free_page(phys);
                }
                ERROR("Failed to allocate stack page at 0x%llx", page_addr);
            }
        }
    }

    /* Unhandled fault */
    cli();

    kprintf("\n");
    kprintf("!!! PAGE FAULT !!!\n");
    kprintf("Fault address: 0x%016llx\n", fault_addr);
    kprintf("Error code:    0x%llx\n", error);
    kprintf("Fault type:    %s %s %s\n",
            is_present ? "protection" : "not-present",
            is_write ? "write" : "read",
            is_user ? "user" : "kernel");
    kprintf("\n");

    /* Print registers */
    dump_registers(state);

    kprintf("\nStack trace:\n");
    stack_trace_from(state->rbp, state->rip);

    /* If user mode fault, kill the process */
    if (is_user) {
        process_t *proc = process_current();
        if (proc) {
            kprintf("\nKilling process %u (%s) due to page fault\n",
                    proc->pid, proc->name);
            process_exit(proc, -11);  /* SIGSEGV */
            /* TODO: trigger reschedule */
        }
    }

    kprintf("\nSystem halted.\n");
    while (1) { cli(); hlt(); }
}

/*
 * Initialize page fault handler
 */
void fault_init(void) {
    INFO("Initializing page fault handler");

    /* Register handler for INT 14 (Page Fault) */
    idt_register_handler(14, (interrupt_handler_t)page_fault_handler);

    INFO("Page fault handler registered");
}
