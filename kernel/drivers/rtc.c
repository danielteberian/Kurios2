/* rtc.c - CMOS Real-Time Clock Driver */

#include "rtc.h"
#include "../arch/x86_64/io.h"
#include "../debug/debug.h"

/* CMOS RTC I/O ports */
#define CMOS_ADDR   0x70
#define CMOS_DATA   0x71

/* CMOS RTC registers */
#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_CENTURY     0x32  /* May not exist on all systems */
#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B

/* Status register B flags */
#define RTC_24HOUR      0x02
#define RTC_BINARY      0x04

/* Cached boot time (Unix timestamp) */
static uint64_t boot_time = 0;

/*
 * Read a CMOS register
 */
static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

/*
 * Check if RTC update is in progress
 */
static int rtc_update_in_progress(void) {
    return cmos_read(RTC_STATUS_A) & 0x80;
}

/*
 * Convert BCD to binary
 */
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

/*
 * Check if year is a leap year
 */
static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/*
 * Days in each month (non-leap year)
 */
static const int days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * Convert date/time to Unix timestamp
 */
static uint64_t datetime_to_unix(int year, int month, int day,
                                  int hour, int min, int sec) {
    uint64_t days = 0;

    /* Days from 1970 to start of this year */
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    /* Days from start of year to start of this month */
    for (int m = 1; m < month; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && is_leap_year(year)) {
            days += 1;  /* February in leap year */
        }
    }

    /* Days in this month */
    days += day - 1;

    /* Convert to seconds and add time */
    return days * 86400ULL + hour * 3600 + min * 60 + sec;
}

/*
 * Read current time from RTC
 */
void rtc_read_time(rtc_time_t *time) {
    uint8_t status_b;
    uint8_t second, minute, hour, day, month, year, century = 20;

    /* Wait for update to complete */
    while (rtc_update_in_progress());

    /* Read all values */
    second = cmos_read(RTC_SECONDS);
    minute = cmos_read(RTC_MINUTES);
    hour = cmos_read(RTC_HOURS);
    day = cmos_read(RTC_DAY);
    month = cmos_read(RTC_MONTH);
    year = cmos_read(RTC_YEAR);

    /* Try to read century (may not exist) */
    uint8_t cent = cmos_read(RTC_CENTURY);
    if (cent != 0 && cent != 0xFF) {
        century = cent;
    }

    status_b = cmos_read(RTC_STATUS_B);

    /* Convert from BCD if necessary */
    if (!(status_b & RTC_BINARY)) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour = bcd_to_bin(hour & 0x7F) | (hour & 0x80);  /* Preserve PM bit */
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
        century = bcd_to_bin(century);
    }

    /* Convert 12-hour to 24-hour if necessary */
    if (!(status_b & RTC_24HOUR)) {
        if (hour == 12) {
            hour = 0;
        }
        if (hour & 0x80) {
            hour = ((hour & 0x7F) + 12) % 24;
        }
    }

    time->second = second;
    time->minute = minute;
    time->hour = hour;
    time->day = day;
    time->month = month;
    time->year = century * 100 + year;
}

/*
 * Get current Unix timestamp
 */
uint64_t rtc_get_unix_time(void) {
    rtc_time_t time;
    rtc_read_time(&time);
    return datetime_to_unix(time.year, time.month, time.day,
                           time.hour, time.minute, time.second);
}

/*
 * Get time since boot in seconds (using HPET or PIT)
 */
uint64_t rtc_get_time_since_boot(void) {
    extern uint64_t hpet_get_ms(void);
    extern bool hpet_is_available(void);

    if (hpet_is_available()) {
        return hpet_get_ms() / 1000;
    }

    /* Fall back to PIT */
    extern uint64_t pit_get_uptime_ms(void);
    return pit_get_uptime_ms() / 1000;
}

/*
 * Initialize RTC driver
 */
void rtc_init(void) {
    INFO("Initializing RTC driver...");

    /* Read and cache boot time */
    boot_time = rtc_get_unix_time();

    rtc_time_t time;
    rtc_read_time(&time);

    INFO("RTC: %04d-%02d-%02d %02d:%02d:%02d (Unix: %llu)",
         time.year, time.month, time.day,
         time.hour, time.minute, time.second,
         boot_time);
}

/*
 * Get boot time (Unix timestamp)
 */
uint64_t rtc_get_boot_time(void) {
    return boot_time;
}

/*
 * Set boot time offset
 * Used by settimeofday() to adjust system time
 */
void rtc_set_boot_time(uint64_t new_boot_time) {
    boot_time = new_boot_time;
}
