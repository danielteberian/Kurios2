/* test_spinlock.c - Spinlock tests */

#include "test_framework.h"
#include "../sync/spinlock.h"

/* Test basic lock/unlock */
TEST_CASE(spinlock_basic) {
    spinlock_t lock = SPINLOCK_INIT;

    TEST_ASSERT_FALSE(spin_is_locked(&lock));

    spin_lock(&lock);
    TEST_ASSERT_TRUE(spin_is_locked(&lock));

    spin_unlock(&lock);
    TEST_ASSERT_FALSE(spin_is_locked(&lock));
}

/* Test trylock */
TEST_CASE(spinlock_trylock) {
    spinlock_t lock = SPINLOCK_INIT;

    /* First trylock should succeed */
    TEST_ASSERT_TRUE(spin_trylock(&lock));
    TEST_ASSERT_TRUE(spin_is_locked(&lock));

    /* Second trylock should fail (already held) */
    TEST_ASSERT_FALSE(spin_trylock(&lock));

    spin_unlock(&lock);

    /* Now trylock should succeed again */
    TEST_ASSERT_TRUE(spin_trylock(&lock));
    spin_unlock(&lock);
}

/* Test init function */
TEST_CASE(spinlock_init) {
    spinlock_t lock;
    lock.lock = 0xDEADBEEF;  /* Garbage value */

    spin_init(&lock);
    TEST_ASSERT_FALSE(spin_is_locked(&lock));
}

/* Test IRQ-safe operations */
TEST_CASE(spinlock_irqsave) {
    spinlock_t lock = SPINLOCK_INIT;

    /* Enable interrupts first */
    sti();
    TEST_ASSERT_TRUE(interrupts_enabled());

    /* Lock with IRQ save - should disable interrupts */
    uint64_t flags = spin_lock_irqsave(&lock);
    TEST_ASSERT_TRUE(spin_is_locked(&lock));
    TEST_ASSERT_FALSE(interrupts_enabled());

    /* Unlock and restore - should re-enable interrupts */
    spin_unlock_irqrestore(&lock, flags);
    TEST_ASSERT_FALSE(spin_is_locked(&lock));
    TEST_ASSERT_TRUE(interrupts_enabled());

    /* Disable interrupts for rest of kernel */
    cli();
}

/* Test suite */
TEST_SUITE(spinlock) {
    RUN_TEST(spinlock_basic);
    RUN_TEST(spinlock_trylock);
    RUN_TEST(spinlock_init);
    RUN_TEST(spinlock_irqsave);
}
