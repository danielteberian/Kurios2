/* entropy.h - Entropy Pool for Random Number Generation */
#ifndef ENTROPY_H
#define ENTROPY_H

#include <stdint.h>
#include <stddef.h>

/*
 * Initialize entropy pool
 * Should be called early in kernel init
 */
void entropy_init(void);

/*
 * Add entropy from various sources
 * data: pointer to entropy data
 * len: length of data in bytes
 * entropy_bits: estimated bits of entropy
 */
void entropy_add(const void *data, size_t len, uint32_t entropy_bits);

/*
 * Get random bytes from entropy pool
 * buf: destination buffer
 * size: number of bytes to generate
 */
void entropy_get_random_bytes(void *buf, size_t size);

/*
 * Get entropy estimate (bits available)
 */
uint32_t entropy_available(void);

#endif /* ENTROPY_H */
