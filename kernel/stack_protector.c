/* stack_protector.c - Stack smashing protector support */

#include "include/types.h"
#include "debug/debug.h"
#include "arch/x86_64/cpu.h"

/*
 * Stack canary value.
 * Initialized to a compile-time value, then randomized at boot.
 * Contains a null byte to help catch string overflows.
 */
uintptr_t __stack_chk_guard = 0x00000AFF0D0A0D00;

/*
 * Called when stack smashing is detected.
 * This function must never return.
 */
__attribute__((noreturn))
void __stack_chk_fail(void) {
    panic("Stack smashing detected!");
}

/*
 * Simple mixing function (xorshift64)
 */
static uint64_t mix(uint64_t x) {
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    return x * 0x2545F4914F6CDD1DULL;
}

/*
 * Initialize stack protector with randomized canary.
 * Uses multiple entropy sources:
 * - TSC (timestamp counter) - varies each boot
 * - Memory addresses - ASLR if present
 * - CPU state
 */
void stack_protector_init(void) {
    uint64_t entropy = 0;

    /* Source 1: Timestamp counter (high resolution, varies each boot) */
    entropy ^= rdtsc();
    entropy = mix(entropy);

    /* Source 2: Another TSC sample (timing variation) */
    for (volatile int i = 0; i < 100; i++) { }  /* Small delay */
    entropy ^= rdtsc();
    entropy = mix(entropy);

    /* Source 3: Stack pointer (memory layout variation) */
    entropy ^= read_rsp();
    entropy = mix(entropy);

    /* Source 4: CR3 (page table base, varies by run) */
    entropy ^= read_cr3();
    entropy = mix(entropy);

    /* Source 5: Function address (varies with KASLR if implemented) */
    entropy ^= (uint64_t)(uintptr_t)&stack_protector_init;
    entropy = mix(entropy);

    /*
     * Build the canary with good properties:
     * - Byte 0 is always 0x00 (null terminator stops string overflows)
     * - Other bytes are randomized
     */
    __stack_chk_guard = (entropy & 0xFFFFFFFFFFFFFF00ULL);

    /* Ensure it's not zero or too predictable */
    if (__stack_chk_guard == 0) {
        __stack_chk_guard = 0xDEADBEEFCAFE0000ULL;
    }

    INFO("Stack protector initialized (canary: 0x%016llx)", __stack_chk_guard);
}
