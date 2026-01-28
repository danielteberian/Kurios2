/* uaccess.h - User Space Memory Access Validation */
#ifndef _KERNEL_MM_UACCESS_H
#define _KERNEL_MM_UACCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../include/types.h"

/*
 * User space address limits
 * User space: 0x0000000000000000 - 0x00007FFFFFFFFFFF
 */
#define USER_ADDR_MAX   0x00007FFFFFFFFFFFUL

/*
 * Check if a user-space address range is valid
 *
 * Validates that:
 * - The entire range [addr, addr+size) is within user space
 * - The range does not overflow
 * - addr + size does not wrap around
 *
 * @param addr  User-space address to check
 * @param size  Size of the memory region
 * @return true if the range is valid for user access, false otherwise
 */
bool access_ok(const void *addr, size_t size);

/*
 * Copy data from user space to kernel space
 *
 * @param dst   Kernel destination buffer
 * @param src   User source address
 * @param size  Number of bytes to copy
 * @return 0 on success, -EFAULT if user address is invalid
 */
int copy_from_user(void *dst, const void *src, size_t size);

/*
 * Copy data from kernel space to user space
 *
 * @param dst   User destination address
 * @param src   Kernel source buffer
 * @param size  Number of bytes to copy
 * @return 0 on success, -EFAULT if user address is invalid
 */
int copy_to_user(void *dst, const void *src, size_t size);

/*
 * Copy a null-terminated string from user space
 *
 * @param dst   Kernel destination buffer
 * @param src   User source string
 * @param max   Maximum bytes to copy (including null terminator)
 * @return Length of string (excluding null), or -EFAULT on invalid address,
 *         or -ENAMETOOLONG if string exceeds max-1 characters
 */
ssize_t strncpy_from_user(char *dst, const char *src, size_t max);

/*
 * Get the length of a user-space string
 *
 * @param str   User string address
 * @param max   Maximum length to check
 * @return Length of string (excluding null), or -EFAULT on invalid address,
 *         or -ENAMETOOLONG if string exceeds max characters
 */
ssize_t strnlen_user(const char *str, size_t max);

#endif /* _KERNEL_MM_UACCESS_H */
