/* rtc.h - CMOS Real-Time Clock Driver */

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/* RTC time structure */
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

/* Initialize RTC driver */
void rtc_init(void);

/* Read current time from RTC */
void rtc_read_time(rtc_time_t *time);

/* Get current Unix timestamp (seconds since 1970-01-01 00:00:00 UTC) */
uint64_t rtc_get_unix_time(void);

/* Get boot time (Unix timestamp when system booted) */
uint64_t rtc_get_boot_time(void);

/* Get time since boot in seconds */
uint64_t rtc_get_time_since_boot(void);

#endif /* RTC_H */
