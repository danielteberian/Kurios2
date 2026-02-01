/* hpet.h - High Precision Event Timer Driver */
#ifndef _KERNEL_DRIVERS_HPET_H
#define _KERNEL_DRIVERS_HPET_H

#include <stdint.h>
#include <stdbool.h>

/*
 * HPET Register Offsets
 */
#define HPET_CAP_ID         0x000   /* General Capabilities and ID */
#define HPET_CONFIG         0x010   /* General Configuration */
#define HPET_INT_STATUS     0x020   /* General Interrupt Status */
#define HPET_COUNTER        0x0F0   /* Main Counter Value */

/* Timer N registers (N = 0, 1, 2, ...) */
#define HPET_TIMER_CONFIG(n)    (0x100 + 0x20 * (n))  /* Timer N Config/Capabilities */
#define HPET_TIMER_COMPARE(n)   (0x108 + 0x20 * (n))  /* Timer N Comparator Value */
#define HPET_TIMER_FSB_INT(n)   (0x110 + 0x20 * (n))  /* Timer N FSB Interrupt Route */

/*
 * General Capabilities and ID Register (offset 0x000)
 */
#define HPET_CAP_REV_ID_MASK        0xFF            /* Revision ID (bits 0-7) */
#define HPET_CAP_NUM_TIM_SHIFT      8               /* Number of timers - 1 (bits 8-12) */
#define HPET_CAP_NUM_TIM_MASK       0x1F
#define HPET_CAP_COUNT_SIZE         (1ULL << 13)    /* 64-bit counter capable */
#define HPET_CAP_LEG_RT             (1ULL << 15)    /* Legacy replacement route capable */
#define HPET_CAP_PERIOD_SHIFT       32              /* Counter tick period in femtoseconds */

/*
 * General Configuration Register (offset 0x010)
 */
#define HPET_CFG_ENABLE         (1 << 0)    /* Overall enable */
#define HPET_CFG_LEG_RT         (1 << 1)    /* Legacy replacement route */

/*
 * Timer Configuration Register (offset 0x100 + 0x20*n)
 */
#define HPET_TN_INT_TYPE        (1 << 1)    /* 0=edge, 1=level triggered */
#define HPET_TN_INT_ENB         (1 << 2)    /* Interrupt enable */
#define HPET_TN_TYPE            (1 << 3)    /* 0=one-shot, 1=periodic */
#define HPET_TN_PER_CAP         (1 << 4)    /* Periodic capable (read-only) */
#define HPET_TN_SIZE_CAP        (1 << 5)    /* 64-bit capable (read-only) */
#define HPET_TN_VAL_SET         (1 << 6)    /* Set accumulator (periodic mode) */
#define HPET_TN_32MODE          (1 << 8)    /* Force 32-bit mode */
#define HPET_TN_INT_ROUTE_SHIFT 9           /* Interrupt routing (bits 9-13) */
#define HPET_TN_INT_ROUTE_MASK  0x1F
#define HPET_TN_FSB_EN          (1 << 14)   /* FSB interrupt enable */
#define HPET_TN_FSB_CAP         (1 << 15)   /* FSB interrupt capable (read-only) */
#define HPET_TN_INT_ROUTE_CAP_SHIFT 32      /* Interrupt routing capability (read-only) */

/*
 * HPET Information Structure
 */
typedef struct hpet_info {
    bool     valid;             /* HPET initialized successfully */
    uint64_t phys_addr;         /* Physical base address */
    uint64_t virt_addr;         /* Virtual base address */
    uint32_t period_fs;         /* Counter period in femtoseconds */
    uint64_t frequency;         /* Counter frequency in Hz */
    uint8_t  num_timers;        /* Number of timers (1-32) */
    bool     is_64bit;          /* 64-bit counter capable */
    bool     legacy_capable;    /* Legacy replacement capable */
} hpet_info_t;

/*
 * Public API
 */

/* Initialize HPET (requires ACPI to have found HPET address) */
int hpet_init(void);

/* Check if HPET is available */
bool hpet_is_available(void);

/* Get HPET information */
const hpet_info_t *hpet_get_info(void);

/* Read the main counter value */
uint64_t hpet_read_counter(void);

/* Get elapsed time in nanoseconds since HPET init */
uint64_t hpet_get_ns(void);

/* Get elapsed time in microseconds since HPET init */
uint64_t hpet_get_us(void);

/* Get elapsed time in milliseconds since HPET init */
uint64_t hpet_get_ms(void);

/* Spin-wait for specified nanoseconds (busy wait) */
void hpet_delay_ns(uint64_t ns);

/* Spin-wait for specified microseconds (busy wait) */
void hpet_delay_us(uint64_t us);

/* Spin-wait for specified milliseconds (busy wait) */
void hpet_delay_ms(uint64_t ms);

/* Get HPET frequency in Hz */
uint64_t hpet_get_frequency(void);

#ifdef DEBUG_TESTS
/* Run HPET tests */
void hpet_run_tests(void);
#endif

#endif /* _KERNEL_DRIVERS_HPET_H */
