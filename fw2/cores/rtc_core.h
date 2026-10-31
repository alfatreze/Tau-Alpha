/* fw2/cores/rtc_core.h -- the Pocket's wall clock as the player shows it (APF host command 0x0090 latched into MMIO 0x1C4-0x1D0, docs/MMIO_ALLOCATION.md). Pure logic, host-tested
 * (sim/test_p2_rtc.py): validate and decode the BCD date / time words the host sent once at boot, and add the player's uptime to get "the clock now" for a Report, so the Report's
 * time = boot time + UPTIME (the host sends the time once; there is no running clock in the core). No time zone is involved: the BCD words are what the Pocket's own clock shows.
 * BCD date word 0xYYYYMMDD; BCD time word 0x0DHHMMSS (bits 27:24 the day of the week, which this code ignores and does not recompute). */
#ifndef RTC_CORE_H
#define RTC_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint16_t y; uint8_t mo, d, h, mi, s; } rtc_dt_t;

static inline int rtc_bcd_byte_ok(uint32_t b) { return ((b >> 4) & 15u) <= 9u && (b & 15u) <= 9u; }
static inline uint32_t rtc_bcd_byte(uint32_t b)
{
#if BUG == 2
    return (b >> 4) * 16u + (b & 15u);                                   /* the tens digit counted as sixteens */
#else
    return ((b >> 4) & 15u) * 10u + (b & 15u);
#endif
}
static inline int rtc_leap(uint32_t y)
{
#if BUG == 1
    return (y % 4u) == 0u && (y % 100u) != 0u;                           /* the 400-year exception is missing: 2000 is not a leap year */
#else
    return ((y % 4u) == 0u && (y % 100u) != 0u) || (y % 400u) == 0u;
#endif
}
static inline uint32_t rtc_mdays(uint32_t y, uint32_t mo)
{
    static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
#if BUG == 3
    if (mo == 2u) return 28u;                                            /* February never has a 29th */
#endif
    return mo == 2u && rtc_leap(y) ? 29u : md[mo - 1u];
}
/* 1 and *o filled when both words are a real date and time (BCD digits only, a month 1..12, a day inside the month, 00:00:00 to 23:59:59, a year 2000..2099); 0 otherwise (never sent: both words 0). */
static inline int rtc_decode(uint32_t date, uint32_t time, rtc_dt_t *o)
{
    if (!rtc_bcd_byte_ok(date >> 24) || !rtc_bcd_byte_ok(date >> 16) || !rtc_bcd_byte_ok(date >> 8) || !rtc_bcd_byte_ok(date)) return 0;
    if (!rtc_bcd_byte_ok(time >> 16) || !rtc_bcd_byte_ok(time >> 8) || !rtc_bcd_byte_ok(time)) return 0;
    const uint32_t y = rtc_bcd_byte(date >> 24) * 100u + rtc_bcd_byte(date >> 16), mo = rtc_bcd_byte(date >> 8), d = rtc_bcd_byte(date);
    const uint32_t h = rtc_bcd_byte(time >> 16), mi = rtc_bcd_byte(time >> 8), s = rtc_bcd_byte(time);
    if (y < 2000u || y > 2099u || mo < 1u || mo > 12u || d < 1u || d > rtc_mdays(y, mo) || h > 23u || mi > 59u || s > 59u) return 0;
    o->y = (uint16_t)y; o->mo = (uint8_t)mo; o->d = (uint8_t)d; o->h = (uint8_t)h; o->mi = (uint8_t)mi; o->s = (uint8_t)s;
    return 1;
}
/* The date and time `secs` seconds after `in` (carries through minutes, hours, days, months, years, leap years). */
static inline void rtc_add(const rtc_dt_t *in, uint32_t secs, rtc_dt_t *o)
{
    uint32_t t = in->s + (secs % 60u), carry;
    o->s = (uint8_t)(t % 60u); carry = t / 60u; secs /= 60u;
    t = in->mi + (secs % 60u) + carry; o->mi = (uint8_t)(t % 60u); carry = t / 60u; secs /= 60u;
    t = in->h + (secs % 24u) + carry; o->h = (uint8_t)(t % 24u); carry = t / 24u; secs /= 24u;
    uint32_t days = secs + carry, y = in->y, mo = in->mo, d = in->d;
    while (days) {
        const uint32_t left = rtc_mdays(y, mo) - d;                     /* days to the end of this month */
        if (days <= left) { d += days; days = 0; }
        else { days -= left + 1u; d = 1u; if (++mo > 12u) { mo = 1u; y++; } }
    }
    o->y = (uint16_t)y; o->mo = (uint8_t)mo; o->d = (uint8_t)d;
}
static inline char *rtc_put2(char *o, uint32_t v) { *o++ = (char)('0' + (v / 10u) % 10u); *o++ = (char)('0' + v % 10u); return o; }
/* "YYYY-MM-DD HH:MM:SS", 19 characters and a terminator. */
static inline void rtc_fmt(const rtc_dt_t *t, char *o)
{
    o = rtc_put2(o, t->y / 100u); o = rtc_put2(o, t->y % 100u); *o++ = '-'; o = rtc_put2(o, t->mo); *o++ = '-'; o = rtc_put2(o, t->d); *o++ = ' ';
    o = rtc_put2(o, t->h); *o++ = ':'; o = rtc_put2(o, t->mi); *o++ = ':'; o = rtc_put2(o, t->s); *o = 0;
}
/* The text of the clock `uptime_s` seconds after the boot time the host sent: "YYYY-MM-DD HH:MM:SS", or "-" when the host never sent a valid time (an older bitstream, or the command has not come). out: 20 bytes. */
static inline void rtc_clock_text(uint32_t valid, uint32_t date, uint32_t time, uint32_t uptime_s, char *out)
{
    rtc_dt_t b, n;
    if (!valid || !rtc_decode(date, time, &b)) { out[0] = '-'; out[1] = 0; return; }
    rtc_add(&b, uptime_s, &n); rtc_fmt(&n, out);
}
#endif
