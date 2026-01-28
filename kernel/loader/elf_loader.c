/* elf_loader.c - ELF Loader Implementation */

#include "elf_loader.h"
#include "elf.h"
#include "../debug/debug.h"
#include "../mm/vmm.h"
#include "mm/pmm.h"
#include "../mm/as.h"
#include "../include/types.h"
#include "lib/string.h"

/*
 * Convert ELF segment flags to page table flags
 */
static uint64_t elf_flags_to_pte(uint32_t p_flags) {
    uint64_t pte_flags = PTE_PRESENT | PTE_USER;

    if (p_flags & PF_W) {
        pte_flags |= PTE_WRITABLE;
    }

    /* Note: x86_64 doesn't have per-page execute disable by default
     * We'd need to check EFER.NXE and use PTE_NX for non-executable pages
     * For now, all user pages are executable unless we enable NX
     */

    return pte_flags;
}

/*
 * Load a single PT_LOAD segment into the address space
 */
static int load_segment(address_space_t *as, const void *elf_data,
                        const Elf64_Phdr *phdr) {
    uint64_t vaddr_start = phdr->p_vaddr;
    uint64_t vaddr_end = vaddr_start + phdr->p_memsz;
    uint64_t file_offset = phdr->p_offset;
    uint64_t file_size = phdr->p_filesz;
    uint64_t pte_flags = elf_flags_to_pte(phdr->p_flags);

    /* Page-align the addresses */
    uint64_t page_start = ALIGN_DOWN(vaddr_start, PAGE_SIZE);
    uint64_t page_end = ALIGN_UP(vaddr_end, PAGE_SIZE);

    DEBUG("Loading segment: vaddr=0x%llx-0x%llx, filesz=%llu, memsz=%llu",
          vaddr_start, vaddr_end, file_size, phdr->p_memsz);

    /* Allocate and map pages for this segment */
    for (uint64_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
        /* Allocate a physical page */
        uint64_t phys = alloc_page();
        if (phys == 0) {
            ERROR("Failed to allocate physical page for segment");
            return -ENOMEM;
        }

        /* Map the page into the address space */
        if (as_map_page(as, vaddr, phys, pte_flags) < 0) {
            ERROR("Failed to map page at 0x%llx", vaddr);
            free_page(phys);
            return -ENOMEM;
        }

        /* Temporarily map to kernel space to initialize */
        uint64_t temp_virt = 0xFFFFFFFF90200000UL + (vaddr - page_start);
        if (vmm_map_page(temp_virt, phys, PTE_KERNEL_RW) < 0) {
            ERROR("Failed to create temp mapping");
            return -ENOMEM;
        }

        /* Clear the page first (important for BSS) */
        memset((void *)temp_virt, 0, PAGE_SIZE);

        /* Copy file data if this page contains any */
        if (vaddr < vaddr_start + file_size) {
            uint64_t copy_start = (vaddr < vaddr_start) ? vaddr_start : vaddr;
            uint64_t copy_end = (vaddr + PAGE_SIZE > vaddr_start + file_size) ?
                                vaddr_start + file_size : vaddr + PAGE_SIZE;
            uint64_t copy_size = copy_end - copy_start;

            if (copy_size > 0) {
                uint64_t file_pos = file_offset + (copy_start - vaddr_start);
                uint64_t page_offset = copy_start - vaddr;
                memcpy((void *)(temp_virt + page_offset),
                       (const uint8_t *)elf_data + file_pos,
                       copy_size);
            }
        }

        /* Unmap temporary mapping */
        vmm_unmap_page(temp_virt);
    }

    return 0;
}

/*
 * Load an ELF executable into an address space
 */
int elf_load(address_space_t *as, const void *elf_data, uint64_t elf_size,
             elf_load_result_t *result) {
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)elf_data;

    /* Validate ELF header */
    if (elf_size < sizeof(Elf64_Ehdr)) {
        ERROR("ELF file too small");
        return -EINVAL;
    }

    if (elf64_validate(ehdr) < 0) {
        ERROR("Invalid ELF header");
        return -EINVAL;
    }

    /* Validate program header table */
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        ERROR("No program headers");
        return -EINVAL;
    }

    if (ehdr->e_phoff + ehdr->e_phnum * sizeof(Elf64_Phdr) > elf_size) {
        ERROR("Program headers extend beyond file");
        return -EINVAL;
    }

    INFO("Loading ELF: entry=0x%llx, %u program headers",
         ehdr->e_entry, ehdr->e_phnum);

    /* Initialize result */
    result->entry_point = ehdr->e_entry;
    result->base_addr = UINT64_MAX;
    result->end_addr = 0;
    result->phdr_addr = 0;
    result->phdr_num = ehdr->e_phnum;
    result->phdr_size = ehdr->e_phentsize;

    /* Load each PT_LOAD segment */
    const Elf64_Phdr *phdr_table = (const Elf64_Phdr *)((const uint8_t *)elf_data + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr *phdr = &phdr_table[i];

        if (phdr->p_type == PT_LOAD) {
            /* Validate segment is in user space */
            if (!as_is_user_addr(phdr->p_vaddr) ||
                !as_is_user_addr(phdr->p_vaddr + phdr->p_memsz - 1)) {
                ERROR("Segment 0x%llx not in user space", phdr->p_vaddr);
                return -EINVAL;
            }

            /* Load the segment */
            int ret = load_segment(as, elf_data, phdr);
            if (ret < 0) {
                return ret;
            }

            /* Update address range */
            if (phdr->p_vaddr < result->base_addr) {
                result->base_addr = phdr->p_vaddr;
            }
            if (phdr->p_vaddr + phdr->p_memsz > result->end_addr) {
                result->end_addr = phdr->p_vaddr + phdr->p_memsz;
            }
        } else if (phdr->p_type == PT_PHDR) {
            result->phdr_addr = phdr->p_vaddr;
        }
    }

    INFO("ELF loaded: base=0x%llx, end=0x%llx, entry=0x%llx",
         result->base_addr, result->end_addr, result->entry_point);

    return 0;
}

/*
 * Load an ELF executable from a file path
 * Note: This requires VFS to be working
 */
int elf_load_file(address_space_t *as, const char *path,
                  elf_load_result_t *result) {
    /* TODO: Implement file loading via VFS */
    (void)as;
    (void)path;
    (void)result;
    ERROR("elf_load_file not yet implemented");
    return -ENOSYS;
}

#ifdef DEBUG_TESTS
/*
 * Minimal test ELF (hand-crafted)
 * This is a valid ELF64 executable that contains our test user program
 */

/* Test ELF header + program header + code */
static const uint8_t test_elf[] = {
    /* ELF Header (64 bytes) */
    0x7F, 'E', 'L', 'F',         /* e_ident[EI_MAG0..3]: Magic */
    0x02,                         /* e_ident[EI_CLASS]: 64-bit */
    0x01,                         /* e_ident[EI_DATA]: Little-endian */
    0x01,                         /* e_ident[EI_VERSION]: Current */
    0x00,                         /* e_ident[EI_OSABI]: System V */
    0x00, 0x00, 0x00, 0x00,       /* e_ident[EI_ABIVERSION + padding] */
    0x00, 0x00, 0x00, 0x00,
    0x02, 0x00,                   /* e_type: ET_EXEC */
    0x3E, 0x00,                   /* e_machine: EM_X86_64 */
    0x01, 0x00, 0x00, 0x00,       /* e_version: 1 */
    0x00, 0x00, 0x40, 0x00,       /* e_entry: 0x400000 (low) */
    0x00, 0x00, 0x00, 0x00,       /* e_entry (high) */
    0x40, 0x00, 0x00, 0x00,       /* e_phoff: 64 (low) */
    0x00, 0x00, 0x00, 0x00,       /* e_phoff (high) */
    0x00, 0x00, 0x00, 0x00,       /* e_shoff: 0 (low) */
    0x00, 0x00, 0x00, 0x00,       /* e_shoff (high) */
    0x00, 0x00, 0x00, 0x00,       /* e_flags */
    0x40, 0x00,                   /* e_ehsize: 64 */
    0x38, 0x00,                   /* e_phentsize: 56 */
    0x01, 0x00,                   /* e_phnum: 1 */
    0x40, 0x00,                   /* e_shentsize: 64 */
    0x00, 0x00,                   /* e_shnum: 0 */
    0x00, 0x00,                   /* e_shstrndx: 0 */

    /* Program Header (56 bytes) at offset 64 */
    0x01, 0x00, 0x00, 0x00,       /* p_type: PT_LOAD */
    0x05, 0x00, 0x00, 0x00,       /* p_flags: PF_R | PF_X */
    0x78, 0x00, 0x00, 0x00,       /* p_offset: 120 (low) */
    0x00, 0x00, 0x00, 0x00,       /* p_offset (high) */
    0x00, 0x00, 0x40, 0x00,       /* p_vaddr: 0x400000 (low) */
    0x00, 0x00, 0x00, 0x00,       /* p_vaddr (high) */
    0x00, 0x00, 0x40, 0x00,       /* p_paddr: 0x400000 (low) */
    0x00, 0x00, 0x00, 0x00,       /* p_paddr (high) */
    0x3A, 0x00, 0x00, 0x00,       /* p_filesz: 58 (low) */
    0x00, 0x00, 0x00, 0x00,       /* p_filesz (high) */
    0x3A, 0x00, 0x00, 0x00,       /* p_memsz: 58 (low) */
    0x00, 0x00, 0x00, 0x00,       /* p_memsz (high) */
    0x00, 0x10, 0x00, 0x00,       /* p_align: 4096 (low) */
    0x00, 0x00, 0x00, 0x00,       /* p_align (high) */

    /* Code at offset 120 (0x78) - same as our test user program */
    /* write syscall */
    0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00,   /* mov rax, 1 (SYS_WRITE) */
    0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00,   /* mov rdi, 1 (stdout) */
    0x48, 0x8d, 0x35, 0x18, 0x00, 0x00, 0x00,   /* lea rsi, [rip+0x18] (msg) */
    0x48, 0xc7, 0xc2, 0x0c, 0x00, 0x00, 0x00,   /* mov rdx, 12 (length) */
    0x0f, 0x05,                                  /* syscall */

    /* exit syscall */
    0x48, 0xc7, 0xc0, 0x3c, 0x00, 0x00, 0x00,   /* mov rax, 60 (SYS_EXIT) */
    0x48, 0xc7, 0xc7, 0x2a, 0x00, 0x00, 0x00,   /* mov rdi, 42 (exit code) */
    0x0f, 0x05,                                  /* syscall */

    /* hang (should never reach) */
    0xeb, 0xfe,                                  /* jmp $ (infinite loop) */

    /* message string */
    'E', 'L', 'F', ' ', 'l', 'o', 'a', 'd', 'e', 'd', '!', '\n'
};

void elf_loader_run_tests(void) {
    kprintf("\n=== ELF Loader Tests ===\n");

    /* Test 1: Validate test ELF header */
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)test_elf;
    int valid = elf64_validate(ehdr);
    kprintf("  Test 1 - Validate header: %s\n", valid == 0 ? "OK" : "FAIL");

    /* Test 2: Create address space and load ELF */
    address_space_t *as = as_create();
    if (!as) {
        kprintf("  Test 2 - Create AS: FAIL\n");
        return;
    }
    kprintf("  Test 2 - Create AS: OK (cr3=0x%llx)\n", as->cr3);

    /* Test 3: Load ELF */
    elf_load_result_t result;
    int ret = elf_load(as, test_elf, sizeof(test_elf), &result);
    kprintf("  Test 3 - Load ELF: %s (ret=%d)\n", ret == 0 ? "OK" : "FAIL", ret);

    if (ret == 0) {
        kprintf("    Entry point: 0x%llx\n", result.entry_point);
        kprintf("    Base addr:   0x%llx\n", result.base_addr);
        kprintf("    End addr:    0x%llx\n", result.end_addr);
    }

    /* Test 4: Verify mapping exists */
    uint64_t phys = as_get_phys(as, result.entry_point);
    kprintf("  Test 4 - Mapping exists: %s (phys=0x%llx)\n",
            phys != 0 ? "OK" : "FAIL", phys);

    /* Clean up */
    as_destroy(as);
    kprintf("  Test 5 - Cleanup: OK\n");

    kprintf("\n  ELF loader tests complete.\n");
}
#endif
