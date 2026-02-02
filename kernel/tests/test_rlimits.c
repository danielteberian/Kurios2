/* test_rlimits.c - Resource Limits Tests */

#include "test_framework.h"
#include "../process/process.h"

TEST_CASE(rlimit_defaults) {
    process_t *proc = process_current();
    TEST_ASSERT_NOT_NULL(proc);

    /* Check default RLIMIT_NOFILE */
    TEST_ASSERT_EQ(proc->limits[RLIMIT_NOFILE].rlim_cur, 1024);
    TEST_ASSERT_EQ(proc->limits[RLIMIT_NOFILE].rlim_max, 4096);

    /* Check default RLIMIT_NPROC */
    TEST_ASSERT_EQ(proc->limits[RLIMIT_NPROC].rlim_cur, 256);
    TEST_ASSERT_EQ(proc->limits[RLIMIT_NPROC].rlim_max, 512);

    /* Check default RLIMIT_STACK */
    TEST_ASSERT_EQ(proc->limits[RLIMIT_STACK].rlim_cur, 8 * 1024 * 1024);
    TEST_ASSERT_EQ(proc->limits[RLIMIT_STACK].rlim_max, 16 * 1024 * 1024);

    /* Check RLIMIT_AS (address space) is unlimited by default */
    TEST_ASSERT_EQ(proc->limits[RLIMIT_AS].rlim_cur, RLIM_INFINITY);
    TEST_ASSERT_EQ(proc->limits[RLIMIT_AS].rlim_max, RLIM_INFINITY);
}

TEST_CASE(rlimit_modify) {
    process_t *proc = process_current();
    TEST_ASSERT_NOT_NULL(proc);

    /* Save original limits */
    struct rlimit orig_nofile = proc->limits[RLIMIT_NOFILE];

    /* Modify soft limit */
    proc->limits[RLIMIT_NOFILE].rlim_cur = 512;
    TEST_ASSERT_EQ(proc->limits[RLIMIT_NOFILE].rlim_cur, 512);
    TEST_ASSERT_EQ(proc->limits[RLIMIT_NOFILE].rlim_max, orig_nofile.rlim_max);

    /* Restore original */
    proc->limits[RLIMIT_NOFILE] = orig_nofile;
}

TEST_CASE(rlimit_bounds) {
    process_t *proc = process_current();
    TEST_ASSERT_NOT_NULL(proc);

    /* Soft limit should never exceed hard limit */
    TEST_ASSERT(proc->limits[RLIMIT_NOFILE].rlim_cur <=
                proc->limits[RLIMIT_NOFILE].rlim_max);
    TEST_ASSERT(proc->limits[RLIMIT_NPROC].rlim_cur <=
                proc->limits[RLIMIT_NPROC].rlim_max);
    TEST_ASSERT(proc->limits[RLIMIT_STACK].rlim_cur <=
                proc->limits[RLIMIT_STACK].rlim_max);
}

TEST_SUITE(rlimits) {
    RUN_TEST(rlimit_defaults);
    RUN_TEST(rlimit_modify);
    RUN_TEST(rlimit_bounds);
}
