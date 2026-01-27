/* test_all.c - Main Test Runner */

#include "test_framework.h"

/* External test suite declarations */
extern void test_suite_pmm(void);
extern void test_suite_vmm(void);
extern void test_suite_slab(void);
extern void test_suite_spinlock(void);

/*
 * Run all kernel tests
 * Called from kernel_main when TEST_MODE is defined
 */
void run_all_tests(void) {
    test_init();

    /* Run all test suites */
    RUN_SUITE(pmm);
    RUN_SUITE(vmm);
    RUN_SUITE(slab);
    RUN_SUITE(spinlock);

    /* Print final results */
    test_print_results();
}
