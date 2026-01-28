/* uaccess.c - User Space Memory Access Validation */

#include "uaccess.h"
#include "as.h"
#include "../include/types.h"
#include "../lib/string.h"
#include "../debug/debug.h"

/*
 * Check if a user-space address range is valid
 *
 * This performs bounds checking only. For a production kernel,
 * you might also want to verify the pages are actually mapped,
 * but that adds overhead and page faults will catch unmapped access.
 */
bool access_ok(const void *addr, size_t size) {
    uint64_t start = (uint64_t)addr;
    uint64_t end;

    /* Zero-size access is always OK */
    if (size == 0) {
        return true;
    }

    /* Check for overflow */
    if (__builtin_add_overflow(start, size - 1, &end)) {
        return false;
    }

    /* Check that entire range is in user space */
    if (start > USER_ADDR_MAX || end > USER_ADDR_MAX) {
        return false;
    }

    /* Check that we're not in the non-canonical hole */
    /* Addresses 0x0000800000000000 - 0xFFFF7FFFFFFFFFFF are invalid */
    if (start >= 0x0000800000000000UL && start < 0xFFFF800000000000UL) {
        return false;
    }
    if (end >= 0x0000800000000000UL && end < 0xFFFF800000000000UL) {
        return false;
    }

    return true;
}

/*
 * Copy data from user space to kernel space
 */
int copy_from_user(void *dst, const void *src, size_t size) {
    if (!dst) {
        return -EFAULT;
    }

    if (size == 0) {
        return 0;
    }

    if (!access_ok(src, size)) {
        WARN("copy_from_user: invalid user address %p, size %llu", src, (unsigned long long)size);
        return -EFAULT;
    }

    /*
     * Note: In a production kernel, this copy should be done with
     * special instructions or exception handling to catch page faults
     * gracefully. For now, we just do a direct copy after validation.
     *
     * The user pages should be mapped if we're in a syscall context
     * (the process's address space is active).
     */
    memcpy(dst, src, size);
    return 0;
}

/*
 * Copy data from kernel space to user space
 */
int copy_to_user(void *dst, const void *src, size_t size) {
    if (!src) {
        return -EFAULT;
    }

    if (size == 0) {
        return 0;
    }

    if (!access_ok(dst, size)) {
        WARN("copy_to_user: invalid user address %p, size %llu", dst, (unsigned long long)size);
        return -EFAULT;
    }

    /*
     * Same note as copy_from_user - in production, this should handle
     * page faults gracefully.
     */
    memcpy(dst, src, size);
    return 0;
}

/*
 * Copy a null-terminated string from user space
 *
 * Copies up to max-1 characters plus null terminator.
 * Returns the length of the string (excluding null), or negative error.
 */
ssize_t strncpy_from_user(char *dst, const char *src, size_t max) {
    size_t len;

    if (!dst || max == 0) {
        return -EFAULT;
    }

    /* Check if source address is at least possibly valid */
    if (!access_ok(src, 1)) {
        return -EFAULT;
    }

    /*
     * Copy character by character, checking bounds as we go.
     * This is safer than computing strlen first, as the string
     * might not be null-terminated within valid memory.
     */
    for (len = 0; len < max - 1; len++) {
        /* Check each character's address */
        if (!access_ok(src + len, 1)) {
            return -EFAULT;
        }

        dst[len] = src[len];
        if (dst[len] == '\0') {
            return (ssize_t)len;
        }
    }

    /* String too long - null-terminate and return error */
    dst[max - 1] = '\0';
    return -ENAMETOOLONG;
}

/*
 * Get the length of a user-space string
 */
ssize_t strnlen_user(const char *str, size_t max) {
    size_t len;

    if (!access_ok(str, 1)) {
        return -EFAULT;
    }

    for (len = 0; len < max; len++) {
        if (!access_ok(str + len, 1)) {
            return -EFAULT;
        }

        if (str[len] == '\0') {
            return (ssize_t)len;
        }
    }

    /* String exceeds max length */
    return -ENAMETOOLONG;
}

#ifdef DEBUG_TESTS
/*
 * Run user access validation tests
 */
void uaccess_run_tests(void) {
    kprintf("\n=== User Access Validation Tests ===\n");

    /* Test 1: Valid user address */
    bool ok = access_ok((void *)0x1000, 0x1000);
    kprintf("  Test 1 - Valid user addr (0x1000, 0x1000): %s\n",
            ok ? "OK" : "FAIL");

    /* Test 2: NULL address with size */
    ok = access_ok(NULL, 0x1000);
    kprintf("  Test 2 - NULL addr with size: %s\n",
            !ok ? "OK" : "FAIL");

    /* Test 3: Zero size (should be OK) */
    ok = access_ok((void *)0x1000, 0);
    kprintf("  Test 3 - Zero size: %s\n",
            ok ? "OK" : "FAIL");

    /* Test 4: Kernel address (should fail) */
    ok = access_ok((void *)0xFFFFFFFF80000000UL, 0x1000);
    kprintf("  Test 4 - Kernel addr: %s\n",
            !ok ? "OK" : "FAIL");

    /* Test 5: Address at user/kernel boundary */
    ok = access_ok((void *)USER_ADDR_MAX, 2);
    kprintf("  Test 5 - Boundary overflow: %s\n",
            !ok ? "OK" : "FAIL");

    /* Test 6: Address in non-canonical hole */
    ok = access_ok((void *)0x0000800000000000UL, 0x1000);
    kprintf("  Test 6 - Non-canonical hole: %s\n",
            !ok ? "OK" : "FAIL");

    /* Test 7: Max user address with size 1 */
    ok = access_ok((void *)USER_ADDR_MAX, 1);
    kprintf("  Test 7 - Max user addr, size 1: %s\n",
            ok ? "OK" : "FAIL");

    /* Test 8: Overflow check */
    ok = access_ok((void *)0xFFFFFFFFFFFFFF00UL, 0x200);
    kprintf("  Test 8 - Address overflow: %s\n",
            !ok ? "OK" : "FAIL");

    kprintf("  User access tests complete.\n");
}
#endif /* DEBUG_TESTS */
