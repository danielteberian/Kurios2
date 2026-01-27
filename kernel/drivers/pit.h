/* pit.h - Programmable Interval Timer (8253/8254) */
#ifndef _DRIVERS_PIT_H
#define _DRIVERS_PIT_H

#include <stdint.h>

/* PIT runs at 1.193182 MHz */
#define PIT_FREQUENCY 1193182

/* Initialize PIT with given frequency (Hz) */
void pit_init(uint32_t frequency);

/* Get tick count since boot */
uint64_t pit_get_ticks(void);

/* Get uptime in milliseconds */
uint64_t pit_get_uptime_ms(void);

/* Sleep for specified milliseconds (busy wait) */
void pit_sleep_ms(uint32_t ms);

/* Get configured frequency */
uint32_t pit_get_frequency(void);

#endif /* _DRIVERS_PIT_H */
