/* test_vmm.c - Virtual Memory Manager Tests */

#include "test_framework.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"

/* Test virtual address range (unused kernel space) */
#define TEST_VIRT_BASE  0xFFFFFFFF90000000UL

/*
 * Test: Verify existing kernel mapping
 */
TEST_CASE(vmm_kernel_mapping_exists) {
    uint64_t kernel_virt = KERNEL_VIRT_BASE;
    uint64_t kernel_phys = vmm_get_phys(kernel_virt);

    TEST_ASSERT_EQ(kernel_phys, KERNEL_PHYS_BASE);
    TEST_ASSERT_TRUE(vmm_is_mapped(kernel_virt));
}

/*
 * Test: Map a single page
 */
TEST_CASE(vmm_map_single_page) {
    uint64_t phys = alloc_page();
    uint64_t virt = TEST_VIRT_BASE;

    TEST_ASSERT_NOT_NULL((void *)phys);

    /* Should not be mapped initially */
    TEST_ASSERT_FALSE(vmm_is_mapped(virt));

    /* Map it */
    int result = vmm_map_page(virt, phys, PTE_KERNEL_RW);
    TEST_ASSERT_EQ(result, 0);

    /* Verify mapping */
    TEST_ASSERT_TRUE(vmm_is_mapped(virt));
    TEST_ASSERT_EQ(vmm_get_phys(virt), phys);

    /* Clean up */
    vmm_unmap_page(virt);
    free_page(phys);
}

/*
 * Test: Write and read through mapping
 */
TEST_CASE(vmm_read_write) {
    uint64_t phys = alloc_page();
    uint64_t virt = TEST_VIRT_BASE + 0x1000;

    vmm_map_page(virt, phys, PTE_KERNEL_RW);

    /* Write a pattern */
    volatile uint64_t *ptr = (volatile uint64_t *)virt;
    *ptr = 0xDEADBEEFCAFEBABEULL;

    /* Read it back */
    uint64_t value = *ptr;
    TEST_ASSERT_EQ(value, 0xDEADBEEFCAFEBABEULL);

    /* Write multiple values */
    for (int i = 0; i < 512; i++) {
        ptr[i] = (uint64_t)i * 0x1234567890ABCDEFULL;
    }

    /* Verify */
    for (int i = 0; i < 512; i++) {
        TEST_ASSERT_EQ(ptr[i], (uint64_t)i * 0x1234567890ABCDEFULL);
    }

    vmm_unmap_page(virt);
    free_page(phys);
}

/*
 * Test: Unmap page
 */
TEST_CASE(vmm_unmap_page) {
    uint64_t phys = alloc_page();
    uint64_t virt = TEST_VIRT_BASE + 0x2000;

    vmm_map_page(virt, phys, PTE_KERNEL_RW);
    TEST_ASSERT_TRUE(vmm_is_mapped(virt));

    /* Unmap returns the physical address */
    uint64_t unmapped_phys = vmm_unmap_page(virt);
    TEST_ASSERT_EQ(unmapped_phys, phys);

    /* Should no longer be mapped */
    TEST_ASSERT_FALSE(vmm_is_mapped(virt));
    TEST_ASSERT_EQ(vmm_get_phys(virt), 0);

    free_page(phys);
}

/*
 * Test: Map multiple contiguous pages
 */
TEST_CASE(vmm_map_multiple_pages) {
    uint64_t phys = alloc_pages(2);  /* 4 pages */
    uint64_t virt = TEST_VIRT_BASE + 0x10000;

    int result = vmm_map_pages(virt, phys, 4, PTE_KERNEL_RW);
    TEST_ASSERT_EQ(result, 0);

    /* Verify all 4 pages */
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(vmm_is_mapped(virt + i * PAGE_SIZE));
        TEST_ASSERT_EQ(vmm_get_phys(virt + i * PAGE_SIZE), phys + i * PAGE_SIZE);
    }

    /* Write to each page */
    for (int i = 0; i < 4; i++) {
        volatile uint64_t *ptr = (volatile uint64_t *)(virt + i * PAGE_SIZE);
        *ptr = 0x1000 + i;
    }

    /* Verify writes */
    for (int i = 0; i < 4; i++) {
        volatile uint64_t *ptr = (volatile uint64_t *)(virt + i * PAGE_SIZE);
        TEST_ASSERT_EQ(*ptr, 0x1000 + i);
    }

    vmm_unmap_pages(virt, 4);
    free_pages(phys, 2);
}

/*
 * Test: Page flags
 */
TEST_CASE(vmm_page_flags) {
    uint64_t phys = alloc_page();
    uint64_t virt = TEST_VIRT_BASE + 0x20000;

    /* Map with specific flags */
    vmm_map_page(virt, phys, PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL);

    pte_t *pte = vmm_get_pte(virt, false);
    TEST_ASSERT_NOT_NULL(pte);

    /* Check flags are set */
    TEST_ASSERT_TRUE(*pte & PTE_PRESENT);
    TEST_ASSERT_TRUE(*pte & PTE_WRITABLE);
    TEST_ASSERT_TRUE(*pte & PTE_GLOBAL);
    TEST_ASSERT_FALSE(*pte & PTE_USER);

    vmm_unmap_page(virt);
    free_page(phys);
}

/*
 * Test: PTE retrieval with auto-creation
 */
TEST_CASE(vmm_get_pte_create) {
    uint64_t virt = TEST_VIRT_BASE + 0x30000;

    /* Get PTE without create - may or may not exist */
    (void)vmm_get_pte(virt, false);

    /* Get PTE with create - should always succeed */
    pte_t *pte2 = vmm_get_pte(virt, true);
    TEST_ASSERT_NOT_NULL(pte2);
}

/*
 * Test: Page table index calculations
 */
TEST_CASE(vmm_index_calculations) {
    uint64_t addr = 0xFFFFFFFF80000000UL;

    /* PML4 index for kernel addresses should be 511 */
    TEST_ASSERT_EQ(PML4_INDEX(addr), 511);

    /* PDPT index for 0xFFFFFFFF80000000 */
    TEST_ASSERT_EQ(PDPT_INDEX(addr), 510);

    /* Different address */
    uint64_t addr2 = 0xFFFFFFFF90000000UL;
    TEST_ASSERT_EQ(PML4_INDEX(addr2), 511);
    TEST_ASSERT_EQ(PDPT_INDEX(addr2), 510);
}

/*
 * Test: TLB flush
 */
TEST_CASE(vmm_tlb_operations) {
    uint64_t phys = alloc_page();
    uint64_t virt = TEST_VIRT_BASE + 0x40000;

    vmm_map_page(virt, phys, PTE_KERNEL_RW);

    /* Write value */
    volatile uint64_t *ptr = (volatile uint64_t *)virt;
    *ptr = 0x12345678;

    /* Flush TLB for this page */
    vmm_flush_page(virt);

    /* Should still be readable */
    TEST_ASSERT_EQ(*ptr, 0x12345678);

    /* Flush entire TLB */
    vmm_flush_tlb();

    /* Should still be readable */
    TEST_ASSERT_EQ(*ptr, 0x12345678);

    vmm_unmap_page(virt);
    free_page(phys);
}

/*
 * Test: Address conversion macros
 */
TEST_CASE(vmm_address_conversion) {
    uint64_t phys = KERNEL_PHYS_BASE;
    uint64_t virt = KERNEL_VIRT_BASE;

    TEST_ASSERT_EQ(KERNEL_PHYS_TO_VIRT(phys), virt);
    TEST_ASSERT_EQ(KERNEL_VIRT_TO_PHYS(virt), phys);

    /* Test offset */
    uint64_t phys2 = KERNEL_PHYS_BASE + 0x1000;
    uint64_t virt2 = KERNEL_VIRT_BASE + 0x1000;

    TEST_ASSERT_EQ(KERNEL_PHYS_TO_VIRT(phys2), virt2);
    TEST_ASSERT_EQ(KERNEL_VIRT_TO_PHYS(virt2), phys2);
}

/*
 * Test: Map/unmap stress test
 */
TEST_CASE(vmm_stress) {
    #define VMM_STRESS_COUNT 50
    uint64_t pages[VMM_STRESS_COUNT];
    uint64_t virts[VMM_STRESS_COUNT];

    /* Allocate and map many pages */
    for (int i = 0; i < VMM_STRESS_COUNT; i++) {
        pages[i] = alloc_page();
        virts[i] = TEST_VIRT_BASE + 0x100000 + i * PAGE_SIZE;

        TEST_ASSERT_NOT_NULL((void *)pages[i]);

        int result = vmm_map_page(virts[i], pages[i], PTE_KERNEL_RW);
        TEST_ASSERT_EQ(result, 0);

        /* Write a marker */
        volatile uint64_t *ptr = (volatile uint64_t *)virts[i];
        *ptr = 0xABCD0000 + i;
    }

    /* Verify all mappings */
    for (int i = 0; i < VMM_STRESS_COUNT; i++) {
        TEST_ASSERT_TRUE(vmm_is_mapped(virts[i]));
        volatile uint64_t *ptr = (volatile uint64_t *)virts[i];
        TEST_ASSERT_EQ(*ptr, 0xABCD0000 + i);
    }

    /* Unmap and free all */
    for (int i = 0; i < VMM_STRESS_COUNT; i++) {
        vmm_unmap_page(virts[i]);
        free_page(pages[i]);
    }

    /* Verify all unmapped */
    for (int i = 0; i < VMM_STRESS_COUNT; i++) {
        TEST_ASSERT_FALSE(vmm_is_mapped(virts[i]));
    }

    #undef VMM_STRESS_COUNT
}

/*
 * VMM Test Suite
 */
TEST_SUITE(vmm) {
    RUN_TEST(vmm_kernel_mapping_exists);
    RUN_TEST(vmm_map_single_page);
    RUN_TEST(vmm_read_write);
    RUN_TEST(vmm_unmap_page);
    RUN_TEST(vmm_map_multiple_pages);
    RUN_TEST(vmm_page_flags);
    RUN_TEST(vmm_get_pte_create);
    RUN_TEST(vmm_index_calculations);
    RUN_TEST(vmm_tlb_operations);
    RUN_TEST(vmm_address_conversion);
    RUN_TEST(vmm_stress);
}
