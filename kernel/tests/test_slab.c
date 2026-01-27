/* test_slab.c - Slab Allocator Tests */

#include "test_framework.h"
#include "../mm/slab.h"
#include "../mm/pmm.h"

/*
 * Test: Basic kmalloc
 */
TEST_CASE(slab_kmalloc_basic) {
    void *p = kmalloc(32);
    TEST_ASSERT_NOT_NULL(p);
    kfree(p);
}

/*
 * Test: kmalloc various sizes
 */
TEST_CASE(slab_kmalloc_sizes) {
    void *p16 = kmalloc(16);
    void *p32 = kmalloc(32);
    void *p64 = kmalloc(64);
    void *p128 = kmalloc(128);
    void *p256 = kmalloc(256);
    void *p512 = kmalloc(512);
    void *p1k = kmalloc(1024);
    void *p2k = kmalloc(2048);
    void *p4k = kmalloc(4096);

    TEST_ASSERT_NOT_NULL(p16);
    TEST_ASSERT_NOT_NULL(p32);
    TEST_ASSERT_NOT_NULL(p64);
    TEST_ASSERT_NOT_NULL(p128);
    TEST_ASSERT_NOT_NULL(p256);
    TEST_ASSERT_NOT_NULL(p512);
    TEST_ASSERT_NOT_NULL(p1k);
    TEST_ASSERT_NOT_NULL(p2k);
    TEST_ASSERT_NOT_NULL(p4k);

    kfree(p16);
    kfree(p32);
    kfree(p64);
    kfree(p128);
    kfree(p256);
    kfree(p512);
    kfree(p1k);
    kfree(p2k);
    kfree(p4k);
}

/*
 * Test: Multiple allocations return unique addresses
 */
TEST_CASE(slab_kmalloc_unique) {
    void *p1 = kmalloc(64);
    void *p2 = kmalloc(64);
    void *p3 = kmalloc(64);

    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_NOT_NULL(p3);

    TEST_ASSERT_NEQ((uint64_t)p1, (uint64_t)p2);
    TEST_ASSERT_NEQ((uint64_t)p2, (uint64_t)p3);
    TEST_ASSERT_NEQ((uint64_t)p1, (uint64_t)p3);

    kfree(p1);
    kfree(p2);
    kfree(p3);
}

/*
 * Test: Write and read memory
 */
TEST_CASE(slab_kmalloc_readwrite) {
    uint64_t *p = kmalloc(sizeof(uint64_t) * 8);
    TEST_ASSERT_NOT_NULL(p);

    /* Write pattern */
    for (int i = 0; i < 8; i++) {
        p[i] = 0xDEADBEEF00000000ULL | i;
    }

    /* Verify pattern */
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQ(p[i], 0xDEADBEEF00000000ULL | i);
    }

    kfree(p);
}

/*
 * Test: kzalloc returns zeroed memory
 */
TEST_CASE(slab_kzalloc) {
    uint8_t *p = kzalloc(256);
    TEST_ASSERT_NOT_NULL(p);

    /* Verify all bytes are zero */
    bool all_zero = true;
    for (int i = 0; i < 256; i++) {
        if (p[i] != 0) {
            all_zero = false;
            break;
        }
    }
    TEST_ASSERT_TRUE(all_zero);

    kfree(p);
}

/*
 * Test: Free and realloc reuses memory
 */
TEST_CASE(slab_free_reuse) {
    void *p1 = kmalloc(32);
    TEST_ASSERT_NOT_NULL(p1);

    kfree(p1);

    /* Same size allocation should reuse the freed object */
    void *p2 = kmalloc(32);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_EQ((uint64_t)p1, (uint64_t)p2);

    kfree(p2);
}

/*
 * Test: ksize returns correct size
 */
TEST_CASE(slab_ksize) {
    void *p32 = kmalloc(32);
    void *p100 = kmalloc(100);  /* Should round up to 128 */

    TEST_ASSERT_NOT_NULL(p32);
    TEST_ASSERT_NOT_NULL(p100);

    size_t s32 = ksize(p32);
    size_t s100 = ksize(p100);

    /* Should return cache object size (>= requested) */
    TEST_ASSERT(s32 >= 32);
    TEST_ASSERT(s100 >= 100);

    kfree(p32);
    kfree(p100);
}

/*
 * Test: kmalloc(0) returns NULL
 */
TEST_CASE(slab_kmalloc_zero) {
    void *p = kmalloc(0);
    TEST_ASSERT_NULL(p);
}

/*
 * Test: kfree(NULL) is safe
 */
TEST_CASE(slab_kfree_null) {
    kfree(NULL);  /* Should not crash */
    TEST_ASSERT_TRUE(true);  /* If we get here, it passed */
}

/*
 * Test: Create custom cache
 */
TEST_CASE(slab_custom_cache) {
    kmem_cache_t *cache = kmem_cache_create("test-cache", 128, 0, 0);
    TEST_ASSERT_NOT_NULL(cache);

    void *obj1 = kmem_cache_alloc(cache);
    void *obj2 = kmem_cache_alloc(cache);

    TEST_ASSERT_NOT_NULL(obj1);
    TEST_ASSERT_NOT_NULL(obj2);
    TEST_ASSERT_NEQ((uint64_t)obj1, (uint64_t)obj2);

    kmem_cache_free(cache, obj1);
    kmem_cache_free(cache, obj2);

    kmem_cache_destroy(cache);
}

/*
 * Test: Cache with alignment
 */
TEST_CASE(slab_cache_alignment) {
    kmem_cache_t *cache = kmem_cache_create("aligned-cache", 64, 64, 0);
    TEST_ASSERT_NOT_NULL(cache);

    void *obj = kmem_cache_alloc(cache);
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_ALIGNED(obj, 64);

    kmem_cache_free(cache, obj);
    kmem_cache_destroy(cache);
}

/*
 * Test: Stress - many allocations
 */
TEST_CASE(slab_stress_alloc) {
    #define STRESS_COUNT 200
    void *ptrs[STRESS_COUNT];

    /* Allocate many objects of various sizes */
    for (int i = 0; i < STRESS_COUNT; i++) {
        size_t size = 16 + (i % 10) * 32;  /* 16 to 304 bytes */
        ptrs[i] = kmalloc(size);
        TEST_ASSERT_NOT_NULL(ptrs[i]);

        /* Write marker */
        *(uint32_t *)ptrs[i] = 0xCAFE0000 | i;
    }

    /* Verify markers */
    for (int i = 0; i < STRESS_COUNT; i++) {
        TEST_ASSERT_EQ(*(uint32_t *)ptrs[i], 0xCAFE0000 | i);
    }

    /* Free all */
    for (int i = 0; i < STRESS_COUNT; i++) {
        kfree(ptrs[i]);
    }

    #undef STRESS_COUNT
}

/*
 * Test: Mixed allocation/free pattern
 */
TEST_CASE(slab_mixed_pattern) {
    void *p1 = kmalloc(64);
    void *p2 = kmalloc(128);
    kfree(p1);
    void *p3 = kmalloc(64);
    void *p4 = kmalloc(256);
    kfree(p2);
    kfree(p4);
    void *p5 = kmalloc(128);
    kfree(p3);
    kfree(p5);

    /* All freed, test passed if no crash */
    TEST_ASSERT_TRUE(true);
}

/*
 * Test: Fill and empty slabs
 */
TEST_CASE(slab_fill_empty) {
    kmem_cache_t *cache = kmem_cache_create("fill-test", 64, 0, 0);
    TEST_ASSERT_NOT_NULL(cache);

    /* Allocate many objects to fill multiple slabs */
    #define FILL_COUNT 100
    void *objs[FILL_COUNT];

    for (int i = 0; i < FILL_COUNT; i++) {
        objs[i] = kmem_cache_alloc(cache);
        TEST_ASSERT_NOT_NULL(objs[i]);
    }

    /* Free all */
    for (int i = 0; i < FILL_COUNT; i++) {
        kmem_cache_free(cache, objs[i]);
    }

    /* Allocate again - should reuse freed objects */
    for (int i = 0; i < FILL_COUNT; i++) {
        objs[i] = kmem_cache_alloc(cache);
        TEST_ASSERT_NOT_NULL(objs[i]);
    }

    /* Clean up */
    for (int i = 0; i < FILL_COUNT; i++) {
        kmem_cache_free(cache, objs[i]);
    }

    kmem_cache_destroy(cache);
    #undef FILL_COUNT
}

/*
 * Slab Allocator Test Suite
 */
TEST_SUITE(slab) {
    RUN_TEST(slab_kmalloc_basic);
    RUN_TEST(slab_kmalloc_sizes);
    RUN_TEST(slab_kmalloc_unique);
    RUN_TEST(slab_kmalloc_readwrite);
    RUN_TEST(slab_kzalloc);
    RUN_TEST(slab_free_reuse);
    RUN_TEST(slab_ksize);
    RUN_TEST(slab_kmalloc_zero);
    RUN_TEST(slab_kfree_null);
    RUN_TEST(slab_custom_cache);
    RUN_TEST(slab_cache_alignment);
    RUN_TEST(slab_stress_alloc);
    RUN_TEST(slab_mixed_pattern);
    RUN_TEST(slab_fill_empty);
}
