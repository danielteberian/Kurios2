/* hpet.c - High Precision Event Timer Driver */

#include "hpet.h"
#include "../acpi/acpi.h"
#include "../debug/debug.h"
#include "../mm/vmm.h"

/* HPET state */
static hpet_info_t hpet_info;
static volatile uint64_t *hpet_base = NULL;

/* Femtoseconds per second */
#define FS_PER_SEC      1000000000000000ULL
#define FS_PER_NS       1000000ULL
#define FS_PER_US       1000000000ULL
#define FS_PER_MS       1000000000000ULL

/*
 * Read HPET register
 */
static inline uint64_t hpet_read(uint32_t reg)
{
    return *(volatile uint64_t *)((uint8_t *)hpet_base + reg);
}

/*
 * Write HPET register
 */
static inline void hpet_write(uint32_t reg, uint64_t value)
{
    *(volatile uint64_t *)((uint8_t *)hpet_base + reg) = value;
}

/*
 * Initialize HPET
 */
int hpet_init(void)
{
    const acpi_info_t *acpi = acpi_get_info();

    if (!acpi || !acpi->valid || !acpi->has_hpet) {
        DEBUG("HPET: Not available (no ACPI HPET table)");
        return -1;
    }

    hpet_info.phys_addr = acpi->hpet_address;

    /* Map HPET registers to virtual memory */
    hpet_info.virt_addr = 0xFFFFFFFF90102000UL;

    if (vmm_map_page(hpet_info.virt_addr, hpet_info.phys_addr,
                     PTE_KERNEL_RW | PTE_PCD) != 0) {
        ERROR("HPET: Failed to map registers at 0x%llx", hpet_info.phys_addr);
        return -1;
    }

    hpet_base = (volatile uint64_t *)hpet_info.virt_addr;

    /* Read capabilities */
    uint64_t cap = hpet_read(HPET_CAP_ID);

    uint8_t rev_id = cap & HPET_CAP_REV_ID_MASK;
    hpet_info.num_timers = ((cap >> HPET_CAP_NUM_TIM_SHIFT) & HPET_CAP_NUM_TIM_MASK) + 1;
    hpet_info.is_64bit = (cap & HPET_CAP_COUNT_SIZE) != 0;
    hpet_info.legacy_capable = (cap & HPET_CAP_LEG_RT) != 0;
    hpet_info.period_fs = (uint32_t)(cap >> HPET_CAP_PERIOD_SHIFT);

    /* Calculate frequency: freq = 10^15 / period_fs */
    if (hpet_info.period_fs == 0) {
        ERROR("HPET: Invalid period (0)");
        return -1;
    }
    hpet_info.frequency = FS_PER_SEC / hpet_info.period_fs;

    DEBUG("HPET: Rev %u, %u timers, %s counter, period=%u fs, freq=%llu Hz",
          rev_id, hpet_info.num_timers,
          hpet_info.is_64bit ? "64-bit" : "32-bit",
          hpet_info.period_fs, hpet_info.frequency);

    /* Disable HPET before configuration */
    uint64_t config = hpet_read(HPET_CONFIG);
    config &= ~HPET_CFG_ENABLE;
    hpet_write(HPET_CONFIG, config);

    /* Reset the main counter */
    hpet_write(HPET_COUNTER, 0);

    /* Disable all timer interrupts */
    for (int i = 0; i < hpet_info.num_timers; i++) {
        uint64_t timer_cfg = hpet_read(HPET_TIMER_CONFIG(i));
        timer_cfg &= ~HPET_TN_INT_ENB;
        hpet_write(HPET_TIMER_CONFIG(i), timer_cfg);
    }

    /* Enable HPET (start counter) */
    config = hpet_read(HPET_CONFIG);
    config |= HPET_CFG_ENABLE;
    hpet_write(HPET_CONFIG, config);

    hpet_info.valid = true;

    INFO("HPET: Initialized at 0x%llx, %llu MHz, %u timers",
         hpet_info.phys_addr, hpet_info.frequency / 1000000, hpet_info.num_timers);

    return 0;
}

/*
 * Check if HPET is available
 */
bool hpet_is_available(void)
{
    return hpet_info.valid;
}

/*
 * Get HPET information
 */
const hpet_info_t *hpet_get_info(void)
{
    return &hpet_info;
}

/*
 * Read the main counter value
 */
uint64_t hpet_read_counter(void)
{
    if (!hpet_base) {
        return 0;
    }
    return hpet_read(HPET_COUNTER);
}

/*
 * Get elapsed time in nanoseconds since HPET init
 */
uint64_t hpet_get_ns(void)
{
    if (!hpet_info.valid) {
        return 0;
    }

    uint64_t counter = hpet_read_counter();
    /* ns = counter * period_fs / 10^6 */
    /* To avoid overflow, divide period first */
    return counter * (hpet_info.period_fs / 1000) / 1000;
}

/*
 * Get elapsed time in microseconds since HPET init
 */
uint64_t hpet_get_us(void)
{
    if (!hpet_info.valid) {
        return 0;
    }

    uint64_t counter = hpet_read_counter();
    /* us = counter * period_fs / 10^9 */
    return counter * hpet_info.period_fs / FS_PER_US;
}

/*
 * Get elapsed time in milliseconds since HPET init
 */
uint64_t hpet_get_ms(void)
{
    if (!hpet_info.valid) {
        return 0;
    }

    uint64_t counter = hpet_read_counter();
    /* ms = counter * period_fs / 10^12 */
    return counter * hpet_info.period_fs / FS_PER_MS;
}

/*
 * Get HPET frequency in Hz
 */
uint64_t hpet_get_frequency(void)
{
    return hpet_info.frequency;
}

/*
 * Spin-wait for specified nanoseconds
 */
void hpet_delay_ns(uint64_t ns)
{
    if (!hpet_info.valid) {
        return;
    }

    /* Calculate ticks needed: ticks = ns * 10^6 / period_fs */
    uint64_t ticks = ns * FS_PER_NS / hpet_info.period_fs;
    uint64_t start = hpet_read_counter();
    uint64_t target = start + ticks;

    /* Handle 32-bit counter wrap */
    if (!hpet_info.is_64bit) {
        target &= 0xFFFFFFFF;
        while (1) {
            uint64_t now = hpet_read_counter() & 0xFFFFFFFF;
            if (now >= target && now < start) break;
            if (target < start && (now >= target || now < start)) break;
            if (now >= target && target >= start) break;
        }
    } else {
        while (hpet_read_counter() < target) {
            /* Busy wait */
        }
    }
}

/*
 * Spin-wait for specified microseconds
 */
void hpet_delay_us(uint64_t us)
{
    hpet_delay_ns(us * 1000);
}

/*
 * Spin-wait for specified milliseconds
 */
void hpet_delay_ms(uint64_t ms)
{
    hpet_delay_ns(ms * 1000000);
}

#ifdef DEBUG_TESTS
/*
 * Run HPET tests
 */
void hpet_run_tests(void)
{
    kprintf("\n=== HPET Tests ===\n");

    /* Test 1: HPET available */
    kprintf("  Test 1 - HPET available: %s\n",
            hpet_info.valid ? "OK" : "SKIP (not available)");

    if (!hpet_info.valid) {
        kprintf("  (Skipping remaining tests - HPET not available)\n\n");
        return;
    }

    /* Test 2: Counter incrementing */
    uint64_t c1 = hpet_read_counter();
    for (volatile int i = 0; i < 10000; i++);  /* Brief delay */
    uint64_t c2 = hpet_read_counter();
    kprintf("  Test 2 - Counter incrementing: %s (delta=%llu)\n",
            (c2 > c1) ? "OK" : "FAIL", c2 - c1);

    /* Test 3: Frequency reasonable (should be in MHz range) */
    bool freq_ok = (hpet_info.frequency >= 1000000 && hpet_info.frequency <= 1000000000);
    kprintf("  Test 3 - Frequency: %llu Hz %s\n",
            hpet_info.frequency, freq_ok ? "OK" : "WARN");

    /* Test 4: Time conversion (1ms delay) */
    uint64_t ms_before = hpet_get_ms();
    hpet_delay_ms(10);
    uint64_t ms_after = hpet_get_ms();
    uint64_t ms_delta = ms_after - ms_before;
    /* Should be close to 10ms (allow 8-15ms range for tolerance) */
    kprintf("  Test 4 - 10ms delay: measured %llu ms %s\n",
            ms_delta, (ms_delta >= 8 && ms_delta <= 15) ? "OK" : "WARN");

    /* Test 5: Microsecond precision */
    uint64_t us_before = hpet_get_us();
    hpet_delay_us(1000);  /* 1000 us = 1 ms */
    uint64_t us_after = hpet_get_us();
    uint64_t us_delta = us_after - us_before;
    kprintf("  Test 5 - 1000us delay: measured %llu us %s\n",
            us_delta, (us_delta >= 900 && us_delta <= 1500) ? "OK" : "WARN");

    /* Summary */
    kprintf("\n  Summary:\n");
    kprintf("    HPET address:    0x%llx\n", hpet_info.phys_addr);
    kprintf("    Counter:         %s\n", hpet_info.is_64bit ? "64-bit" : "32-bit");
    kprintf("    Timers:          %u\n", hpet_info.num_timers);
    kprintf("    Period:          %u fs\n", hpet_info.period_fs);
    kprintf("    Frequency:       %llu Hz (%llu MHz)\n",
            hpet_info.frequency, hpet_info.frequency / 1000000);
    kprintf("    Legacy capable:  %s\n", hpet_info.legacy_capable ? "yes" : "no");
    kprintf("    Current counter: %llu\n", hpet_read_counter());
    kprintf("    Uptime:          %llu ms\n", hpet_get_ms());
    kprintf("\n");
}
#endif /* DEBUG_TESTS */
