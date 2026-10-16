/* fw2/cores/nowplaying_core.h -- the arithmetic and text of the Now Playing view, kept pure so it is host-tested (sim/test_p2_nowplaying.py): clock text from a pair count and a rate,
 * progress bar width, the tempo and rate labels, and the cut of a long title. The view itself (ui/views.c) only draws what these return. */
#ifndef NOWPLAYING_CORE_H
#define NOWPLAYING_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
/* "M:SS", or "H:MM:SS" from an hour on. Returns the length. Seconds are the floor of pairs / rate (a clock that rounds up shows the next second early). */
static inline uint32_t np_time(char *o, uint32_t pairs, uint32_t rate)
{
    uint32_t s = rate ? pairs / rate : 0u, n = 0;
#if BUG == 1
    s = rate ? (pairs + rate / 2u) / rate : 0u;                       /* rounds up: the clock runs half a second fast */
#endif
    uint32_t h = s / 3600u, m = (s / 60u) % 60u, sec = s % 60u;
    if (h) { o[n++] = (char)('0' + (h > 9u ? 9u : h)); o[n++] = ':'; o[n++] = (char)('0' + m / 10u); }
    else if (m >= 10u) o[n++] = (char)('0' + m / 10u);
    o[n++] = (char)('0' + m % 10u); o[n++] = ':'; o[n++] = (char)('0' + sec / 10u); o[n++] = (char)('0' + sec % 10u); o[n] = 0;
    return n;
}
/* Filled width of a bar `w` pixels wide at pos/total (clamped; an unknown total shows an empty bar). */
static inline uint32_t np_bar(uint32_t pos, uint32_t total, uint32_t w)
{
    if (!total) return 0;
#if BUG == 2
    return (uint32_t)(((uint64_t)pos * w) / total);                   /* no clamp: a position past the end overflows the bar */
#else
    if (pos >= total) return w;
    return (uint32_t)(((uint64_t)pos * w) / total);
#endif
}
/* Tempo label from the Q8 speed: "1.25X" (two decimals, exact for the offered speeds). */
static inline uint32_t np_speed(char *o, uint32_t q8)
{
    uint32_t c = (q8 * 100u + 128u) / 256u;                           /* hundredths */
    uint32_t n = 0; o[n++] = (char)('0' + c / 100u); o[n++] = '.'; o[n++] = (char)('0' + (c / 10u) % 10u); o[n++] = (char)('0' + c % 10u); o[n++] = 'X'; o[n] = 0;
    return n;
}
/* "44.1K" / "48K" from the source rate. */
static inline uint32_t np_khz(char *o, uint32_t hz)
{
    uint32_t t = (hz + 50u) / 100u, n = 0;                            /* tenths of kHz */
    if (t >= 100u) o[n++] = (char)('0' + t / 100u);
    o[n++] = (char)('0' + (t / 10u) % 10u);
    if (t % 10u) { o[n++] = '.'; o[n++] = (char)('0' + t % 10u); }
    o[n++] = 'K'; o[n] = 0;
    return n;
}
#endif
