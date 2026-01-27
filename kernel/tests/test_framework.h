/* test_framework.h - Kernel Test Framework */
#ifndef _KERNEL_TESTS_FRAMEWORK_H
#define _KERNEL_TESTS_FRAMEWORK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../debug/debug.h"

/*
 * Test Framework for Kurios2 Kernel
 *
 * Usage:
 *   TEST_CASE(name) {
 *       TEST_ASSERT(condition);
 *       TEST_ASSERT_EQ(actual, expected);
 *       TEST_ASSERT_NEQ(actual, unexpected);
 *       TEST_ASSERT_NOT_NULL(ptr);
 *   }
 *
 *   TEST_SUITE(suite_name) {
 *       RUN_TEST(test_name);
 *   }
 */

/* Test result tracking */
typedef struct {
    const char *name;
    uint32_t passed;
    uint32_t failed;
    uint32_t total;
} test_suite_t;

typedef struct {
    uint32_t suites_run;
    uint32_t suites_passed;
    uint32_t suites_failed;
    uint32_t tests_passed;
    uint32_t tests_failed;
    uint32_t assertions_passed;
    uint32_t assertions_failed;
} test_results_t;

/* Global test state */
extern test_results_t g_test_results;
extern test_suite_t *g_current_suite;
extern const char *g_current_test;
extern bool g_current_test_failed;

/* Initialize test framework */
void test_init(void);

/* Print final test results */
void test_print_results(void);

/* Internal functions */
void test_suite_begin(test_suite_t *suite, const char *name);
void test_suite_end(test_suite_t *suite);
void test_begin(const char *name);
void test_end(void);
void test_assert_fail(const char *file, int line, const char *expr);
void test_assert_eq_fail(const char *file, int line, uint64_t actual, uint64_t expected);
void test_assert_neq_fail(const char *file, int line, uint64_t actual, uint64_t unexpected);

/*
 * Test case definition macro
 */
#define TEST_CASE(name) \
    static void test_##name(void)

/*
 * Run a test case
 */
#define RUN_TEST(name) \
    do { \
        test_begin(#name); \
        test_##name(); \
        test_end(); \
    } while (0)

/*
 * Test suite definition
 */
#define TEST_SUITE(name) \
    void test_suite_##name(void)

/*
 * Run a test suite
 */
#define RUN_SUITE(name) \
    do { \
        test_suite_t suite; \
        test_suite_begin(&suite, #name); \
        test_suite_##name(); \
        test_suite_end(&suite); \
    } while (0)

/*
 * Assertion macros
 */
#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            test_assert_fail(__FILE__, __LINE__, #expr); \
        } else { \
            g_test_results.assertions_passed++; \
        } \
    } while (0)

#define TEST_ASSERT_EQ(actual, expected) \
    do { \
        uint64_t _a = (uint64_t)(actual); \
        uint64_t _e = (uint64_t)(expected); \
        if (_a != _e) { \
            test_assert_eq_fail(__FILE__, __LINE__, _a, _e); \
        } else { \
            g_test_results.assertions_passed++; \
        } \
    } while (0)

#define TEST_ASSERT_NEQ(actual, unexpected) \
    do { \
        uint64_t _a = (uint64_t)(actual); \
        uint64_t _u = (uint64_t)(unexpected); \
        if (_a == _u) { \
            test_assert_neq_fail(__FILE__, __LINE__, _a, _u); \
        } else { \
            g_test_results.assertions_passed++; \
        } \
    } while (0)

#define TEST_ASSERT_NOT_NULL(ptr) \
    TEST_ASSERT((ptr) != NULL)

#define TEST_ASSERT_NULL(ptr) \
    TEST_ASSERT((ptr) == NULL)

#define TEST_ASSERT_TRUE(expr) \
    TEST_ASSERT(expr)

#define TEST_ASSERT_FALSE(expr) \
    TEST_ASSERT(!(expr))

/* Memory comparison */
#define TEST_ASSERT_MEM_EQ(ptr1, ptr2, size) \
    do { \
        const uint8_t *_p1 = (const uint8_t *)(ptr1); \
        const uint8_t *_p2 = (const uint8_t *)(ptr2); \
        bool _match = true; \
        for (size_t _i = 0; _i < (size); _i++) { \
            if (_p1[_i] != _p2[_i]) { _match = false; break; } \
        } \
        TEST_ASSERT(_match); \
    } while (0)

/* Range check */
#define TEST_ASSERT_IN_RANGE(val, min, max) \
    TEST_ASSERT((val) >= (min) && (val) <= (max))

/* Pointer alignment check */
#define TEST_ASSERT_ALIGNED(ptr, align) \
    TEST_ASSERT(((uint64_t)(ptr) % (align)) == 0)

/*
 * Skip a test (mark as passed but note it was skipped)
 */
#define TEST_SKIP(reason) \
    do { \
        kprintf("    [SKIP] %s\n", reason); \
        return; \
    } while (0)

#endif /* _KERNEL_TESTS_FRAMEWORK_H */
