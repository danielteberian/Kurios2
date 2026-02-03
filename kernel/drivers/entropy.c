/* entropy.c - Entropy Pool Implementation */

#include "entropy.h"
#include "../debug/debug.h"
#include "../arch/x86_64/cpu.h"
#include "../drivers/pit.h"
#include "../drivers/rtc.h"
#include "../drivers/hpet.h"
#include "../lib/string.h"
#include "../sync/spinlock.h"

/*
 * Entropy pool based on SHA-256-like mixing
 * Uses multiple sources: TSC, PIT, RTC, HPET, interrupts
 */

#define POOL_SIZE 512   /* Entropy pool size in bytes */
#define POOL_WORDS (POOL_SIZE / sizeof(uint32_t))

typedef struct {
    uint32_t pool[POOL_WORDS];  /* Main entropy pool */
    uint32_t index;              /* Current write index */
    uint32_t entropy_bits;       /* Estimated entropy (bits) */
    spinlock_t lock;             /* Protection for concurrent access */
} entropy_pool_t;

static entropy_pool_t entropy_pool;

/*
 * Read Time Stamp Counter
 */
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/*
 * Simple mixing function (based on CRC-32 polynomial)
 * This is NOT cryptographic-grade, but good enough for kernel RNG
 */
static uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t result = a ^ b;
    result ^= (result << 13);
    result ^= (result >> 17);
    result ^= (result << 5);
    return result;
}

/*
 * Mix data into the entropy pool
 */
static void mix_pool(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t w = 0;

    for (size_t i = 0; i < len; i++) {
        /* Accumulate bytes into word */
        w = (w << 8) | bytes[i];

        /* Mix every 4 bytes */
        if ((i & 3) == 3) {
            entropy_pool.pool[entropy_pool.index] =
                mix(entropy_pool.pool[entropy_pool.index], w);
            entropy_pool.index = (entropy_pool.index + 1) % POOL_WORDS;
            w = 0;
        }
    }

    /* Mix any remaining bytes */
    if (len & 3) {
        entropy_pool.pool[entropy_pool.index] =
            mix(entropy_pool.pool[entropy_pool.index], w);
        entropy_pool.index = (entropy_pool.index + 1) % POOL_WORDS;
    }

    /* Mix in current position for additional diffusion */
    for (int i = 0; i < 16; i++) {
        uint32_t idx = (entropy_pool.index + i) % POOL_WORDS;
        uint32_t prev = (entropy_pool.index + i - 1) % POOL_WORDS;
        entropy_pool.pool[idx] = mix(entropy_pool.pool[idx],
                                     entropy_pool.pool[prev]);
    }
}

/*
 * Collect entropy from multiple sources
 */
static void collect_entropy(void) {
    uint64_t sources[8];
    int src = 0;

    /* TSC - cycle counter (high resolution, predictable but fast-changing) */
    sources[src++] = rdtsc();

    /* PIT uptime - lower resolution, monotonic */
    sources[src++] = pit_get_uptime_ms();

    /* RTC - wall clock time */
    sources[src++] = rtc_get_unix_time();

    /* HPET if available */
    if (hpet_is_available()) {
        sources[src++] = hpet_read_counter();
    }

    /* Mix current pool state */
    sources[src++] = entropy_pool.pool[entropy_pool.index];
    sources[src++] = entropy_pool.pool[(entropy_pool.index + 7) % POOL_WORDS];

    /* Stack address (ASLR-like randomness) */
    sources[src++] = (uint64_t)&sources;

    /* Current TSC again (for timing variation) */
    sources[src++] = rdtsc();

    /* Mix into pool */
    mix_pool(sources, src * sizeof(uint64_t));
}

/*
 * Initialize entropy pool
 */
void entropy_init(void) {
    spinlock_init(&entropy_pool.lock);
    entropy_pool.index = 0;
    entropy_pool.entropy_bits = 0;

    /* Initialize pool with TSC */
    uint64_t initial_seed = rdtsc();
    for (size_t i = 0; i < POOL_WORDS; i++) {
        initial_seed = mix((uint32_t)initial_seed, (uint32_t)(initial_seed >> 32));
        entropy_pool.pool[i] = (uint32_t)initial_seed;
    }

    /* Collect initial entropy */
    for (int i = 0; i < 10; i++) {
        collect_entropy();
    }

    /* Assume we have at least 128 bits of entropy after init */
    entropy_pool.entropy_bits = 128;

    INFO("Entropy pool initialized (%u bits estimated)", entropy_pool.entropy_bits);
}

/*
 * Add entropy to the pool
 */
void entropy_add(const void *data, size_t len, uint32_t entropy_bits) {
    if (!data || len == 0) {
        return;
    }

    spinlock_acquire(&entropy_pool.lock);

    /* Mix in current time sources for additional randomness */
    uint64_t tsc = rdtsc();
    mix_pool(&tsc, sizeof(tsc));

    /* Mix in provided data */
    mix_pool(data, len);

    /* Update entropy estimate (cap at pool size in bits) */
    entropy_pool.entropy_bits += entropy_bits;
    if (entropy_pool.entropy_bits > POOL_SIZE * 8) {
        entropy_pool.entropy_bits = POOL_SIZE * 8;
    }

    spinlock_release(&entropy_pool.lock);
}

/*
 * Extract random bytes from pool (ChaCha20-like output generation)
 */
void entropy_get_random_bytes(void *buf, size_t size) {
    uint8_t *dst = (uint8_t *)buf;

    spinlock_acquire(&entropy_pool.lock);

    /* Collect fresh entropy before extraction */
    collect_entropy();

    /* Generate random bytes */
    size_t pos = 0;
    while (pos < size) {
        /* Mix pool state for each output word */
        uint32_t output = entropy_pool.pool[entropy_pool.index];

        /* Additional mixing with neighboring words */
        uint32_t next = (entropy_pool.index + 1) % POOL_WORDS;
        uint32_t prev = (entropy_pool.index + POOL_WORDS - 1) % POOL_WORDS;
        output = mix(output, entropy_pool.pool[next]);
        output = mix(output, entropy_pool.pool[prev]);

        /* TSC for additional unpredictability */
        output ^= (uint32_t)rdtsc();

        /* Output bytes */
        for (int i = 0; i < 4 && pos < size; i++) {
            dst[pos++] = (uint8_t)(output >> (i * 8));
        }

        /* Advance pool and remix */
        entropy_pool.index = next;
        entropy_pool.pool[entropy_pool.index] =
            mix(entropy_pool.pool[entropy_pool.index], output);
    }

    /* Debit entropy (conservative estimate: 1 bit per byte consumed) */
    if (entropy_pool.entropy_bits > size * 8) {
        entropy_pool.entropy_bits -= size * 8;
    } else {
        entropy_pool.entropy_bits = 0;
    }

    /* Collect more entropy for future use */
    collect_entropy();

    spinlock_release(&entropy_pool.lock);
}

/*
 * Get entropy estimate
 */
uint32_t entropy_available(void) {
    spinlock_acquire(&entropy_pool.lock);
    uint32_t bits = entropy_pool.entropy_bits;
    spinlock_release(&entropy_pool.lock);
    return bits;
}
