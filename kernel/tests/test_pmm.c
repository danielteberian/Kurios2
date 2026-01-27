/* test_pmm.c - Physical Memory Manager Tests */

#include "test_framework.h"
#include "../mm/pmm.h"

/*
 * Test: Single page allocation
 */
TEST_CASE(pmm_alloc_single_page) {
    uint64_t page = alloc_page();
    TEST_ASSERT_NOT_NULL((void *)page);
    TEST_ASSERT_ALIGNED(page, PAGE_SIZE);

    /* Clean up */
    free_page(page);
}

/*
 * Test: Multiple page allocations return different addresses
 */
TEST_CASE(pmm_alloc_multiple_pages_unique) {
    uint64_t page1 = alloc_page();
    uint64_t page2 = alloc_page();
    uint64_t page3 = alloc_page();

    TEST_ASSERT_NOT_NULL((void *)page1);
    TEST_ASSERT_NOT_NULL((void *)page2);
    TEST_ASSERT_NOT_NULL((void *)page3);

    TEST_ASSERT_NEQ(page1, page2);
    TEST_ASSERT_NEQ(page2, page3);
    TEST_ASSERT_NEQ(page1, page3);

    /* Clean up */
    free_page(page1);
    free_page(page2);
    free_page(page3);
}

/*
 * Test: Order-based allocation (2^order pages)
 */
TEST_CASE(pmm_alloc_order) {
    /* Order 0: 1 page */
    uint64_t order0 = alloc_pages(0);
    TEST_ASSERT_NOT_NULL((void *)order0);
    TEST_ASSERT_ALIGNED(order0, PAGE_SIZE);

    /* Order 2: 4 pages */
    uint64_t order2 = alloc_pages(2);
    TEST_ASSERT_NOT_NULL((void *)order2);
    TEST_ASSERT_ALIGNED(order2, PAGE_SIZE * 4);

    /* Order 5: 32 pages (128KB) */
    uint64_t order5 = alloc_pages(5);
    TEST_ASSERT_NOT_NULL((void *)order5);
    TEST_ASSERT_ALIGNED(order5, PAGE_SIZE * 32);

    /* Clean up */
    free_pages(order0, 0);
    free_pages(order2, 2);
    free_pages(order5, 5);
}

/*
 * Test: Large allocation (4MB = order 10)
 */
TEST_CASE(pmm_alloc_large) {
    uint64_t large = alloc_pages(10);  /* 4MB */
    TEST_ASSERT_NOT_NULL((void *)large);
    TEST_ASSERT_ALIGNED(large, PAGE_SIZE * 1024);

    free_pages(large, 10);
}

/*
 * Test: Free and realloc returns same page (buddy system)
 */
TEST_CASE(pmm_free_realloc) {
    uint64_t page1 = alloc_page();
    TEST_ASSERT_NOT_NULL((void *)page1);

    free_page(page1);

    /* Reallocate - should get the same page back (LIFO free list) */
    uint64_t page2 = alloc_page();
    TEST_ASSERT_NOT_NULL((void *)page2);
    TEST_ASSERT_EQ(page1, page2);

    free_page(page2);
}

/*
 * Test: Page descriptor lookup
 */
TEST_CASE(pmm_page_descriptor) {
    uint64_t phys = alloc_page();
    TEST_ASSERT_NOT_NULL((void *)phys);

    page_t *page = phys_to_page(phys);
    TEST_ASSERT_NOT_NULL(page);

    /* Verify reverse mapping */
    uint64_t phys_back = page_to_phys(page);
    TEST_ASSERT_EQ(phys, phys_back);

    free_page(phys);
}

/*
 * Test: PFN conversion
 */
TEST_CASE(pmm_pfn_conversion) {
    uint64_t phys = 0x200000;  /* 2MB */
    uint64_t pfn = phys_to_pfn(phys);
    TEST_ASSERT_EQ(pfn, 512);  /* 0x200000 / 4096 = 512 */

    uint64_t phys_back = pfn_to_phys(pfn);
    TEST_ASSERT_EQ(phys, phys_back);
}

/*
 * Test: Buddy merging - allocate/free sequence should merge
 */
TEST_CASE(pmm_buddy_merging) {
    uint64_t initial_free = mem_info.free_pages;

    /* Allocate 8 x 128 pages (order 7 = 512KB each) */
    uint64_t addrs[8];
    for (int i = 0; i < 8; i++) {
        addrs[i] = alloc_pages(7);
        TEST_ASSERT_NOT_NULL((void *)addrs[i]);
    }

    uint64_t after_alloc = mem_info.free_pages;
    TEST_ASSERT(after_alloc < initial_free);

    /* Free all in reverse order */
    for (int i = 7; i >= 0; i--) {
        free_pages(addrs[i], 7);
    }

    /* Should be back to initial state */
    uint64_t after_free = mem_info.free_pages;
    TEST_ASSERT_EQ(initial_free, after_free);
}

/*
 * Test: Stress - many small allocations
 */
TEST_CASE(pmm_stress_small) {
    #define SMALL_ALLOC_COUNT 100
    uint64_t pages[SMALL_ALLOC_COUNT];

    /* Allocate many pages */
    for (int i = 0; i < SMALL_ALLOC_COUNT; i++) {
        pages[i] = alloc_page();
        TEST_ASSERT_NOT_NULL((void *)pages[i]);
    }

    /* Verify all unique */
    for (int i = 0; i < SMALL_ALLOC_COUNT; i++) {
        for (int j = i + 1; j < SMALL_ALLOC_COUNT; j++) {
            TEST_ASSERT_NEQ(pages[i], pages[j]);
        }
    }

    /* Free all */
    for (int i = 0; i < SMALL_ALLOC_COUNT; i++) {
        free_page(pages[i]);
    }
    #undef SMALL_ALLOC_COUNT
}

/*
 * PMM Test Suite
 */
TEST_SUITE(pmm) {
    RUN_TEST(pmm_alloc_single_page);
    RUN_TEST(pmm_alloc_multiple_pages_unique);
    RUN_TEST(pmm_alloc_order);
    RUN_TEST(pmm_alloc_large);
    RUN_TEST(pmm_free_realloc);
    RUN_TEST(pmm_page_descriptor);
    RUN_TEST(pmm_pfn_conversion);
    RUN_TEST(pmm_buddy_merging);
    RUN_TEST(pmm_stress_small);
}
