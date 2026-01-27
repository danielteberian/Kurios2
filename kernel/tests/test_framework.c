/* test_framework.c - Kernel Test Framework Implementation */

#include "test_framework.h"
#include "../debug/debug.h"

/* Global test state */
test_results_t g_test_results;
test_suite_t *g_current_suite = NULL;
const char *g_current_test = NULL;
bool g_current_test_failed = false;

/*
 * Initialize test framework
 */
void test_init(void) {
    g_test_results.suites_run = 0;
    g_test_results.suites_passed = 0;
    g_test_results.suites_failed = 0;
    g_test_results.tests_passed = 0;
    g_test_results.tests_failed = 0;
    g_test_results.assertions_passed = 0;
    g_test_results.assertions_failed = 0;

    g_current_suite = NULL;
    g_current_test = NULL;
    g_current_test_failed = false;

    kprintf("\n");
    kprintf("============================================================\n");
    kprintf("                 KURIOS2 KERNEL TEST SUITE                  \n");
    kprintf("============================================================\n");
    kprintf("\n");
}

/*
 * Print final test results
 */
void test_print_results(void) {
    kprintf("\n");
    kprintf("============================================================\n");
    kprintf("                      TEST RESULTS                          \n");
    kprintf("============================================================\n");
    kprintf("\n");

    kprintf("  Suites:     %u passed, %u failed, %u total\n",
            g_test_results.suites_passed,
            g_test_results.suites_failed,
            g_test_results.suites_run);

    kprintf("  Tests:      %u passed, %u failed, %u total\n",
            g_test_results.tests_passed,
            g_test_results.tests_failed,
            g_test_results.tests_passed + g_test_results.tests_failed);

    kprintf("  Assertions: %u passed, %u failed, %u total\n",
            g_test_results.assertions_passed,
            g_test_results.assertions_failed,
            g_test_results.assertions_passed + g_test_results.assertions_failed);

    kprintf("\n");

    if (g_test_results.tests_failed == 0 && g_test_results.suites_failed == 0) {
        kprintf("  *** ALL TESTS PASSED ***\n");
    } else {
        kprintf("  *** SOME TESTS FAILED ***\n");
    }

    kprintf("\n");
    kprintf("============================================================\n");
}

/*
 * Begin a test suite
 */
void test_suite_begin(test_suite_t *suite, const char *name) {
    suite->name = name;
    suite->passed = 0;
    suite->failed = 0;
    suite->total = 0;

    g_current_suite = suite;
    g_test_results.suites_run++;

    kprintf("\n--- Test Suite: %s ---\n", name);
}

/*
 * End a test suite
 */
void test_suite_end(test_suite_t *suite) {
    kprintf("  Suite '%s': %u/%u tests passed\n",
            suite->name, suite->passed, suite->total);

    if (suite->failed == 0) {
        g_test_results.suites_passed++;
    } else {
        g_test_results.suites_failed++;
    }

    g_current_suite = NULL;
}

/*
 * Begin a test case
 */
void test_begin(const char *name) {
    g_current_test = name;
    g_current_test_failed = false;

    if (g_current_suite) {
        g_current_suite->total++;
    }
}

/*
 * End a test case
 */
void test_end(void) {
    if (g_current_test_failed) {
        kprintf("  [FAIL] %s\n", g_current_test);
        g_test_results.tests_failed++;
        if (g_current_suite) {
            g_current_suite->failed++;
        }
    } else {
        kprintf("  [PASS] %s\n", g_current_test);
        g_test_results.tests_passed++;
        if (g_current_suite) {
            g_current_suite->passed++;
        }
    }

    g_current_test = NULL;
}

/*
 * Report assertion failure
 */
void test_assert_fail(const char *file, int line, const char *expr) {
    g_current_test_failed = true;
    g_test_results.assertions_failed++;

    kprintf("    ASSERT FAILED at %s:%d\n", file, line);
    kprintf("      Expression: %s\n", expr);
}

/*
 * Report equality assertion failure
 */
void test_assert_eq_fail(const char *file, int line, uint64_t actual, uint64_t expected) {
    g_current_test_failed = true;
    g_test_results.assertions_failed++;

    kprintf("    ASSERT_EQ FAILED at %s:%d\n", file, line);
    kprintf("      Expected: 0x%llx (%llu)\n", expected, expected);
    kprintf("      Actual:   0x%llx (%llu)\n", actual, actual);
}

/*
 * Report inequality assertion failure
 */
void test_assert_neq_fail(const char *file, int line, uint64_t actual, uint64_t unexpected) {
    g_current_test_failed = true;
    g_test_results.assertions_failed++;

    kprintf("    ASSERT_NEQ FAILED at %s:%d\n", file, line);
    kprintf("      Should not be: 0x%llx (%llu)\n", unexpected, unexpected);
    kprintf("      But got:       0x%llx (%llu)\n", actual, actual);
}
