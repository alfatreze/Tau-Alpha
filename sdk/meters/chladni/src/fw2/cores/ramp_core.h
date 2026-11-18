/* fw2/cores/ramp_core.h -- linear colour ramps in RGB565 (pure, host-tested by sim/test_p2_ramp.py against an independent statement).
 *
 * Specification: the colour at step t of n (0 <= t <= n) between a and b has each of its three channels (5, 6 and 5 bits) at a + (b - a) * t / n, computed channel by channel in integers with every
 * product divided by n rounding toward zero (so t = 0 gives a, t = n gives b, and a channel never leaves the range of its two ends). n = 0 returns a. */
#ifndef RAMP_CORE_H
#define RAMP_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
static inline uint16_t rc_ramp(uint16_t a, uint16_t b, uint32_t t, uint32_t n)
{
    static const uint8_t shift[3] = { 11, 5, 0 }, width[3] = { 5, 6, 5 };
    uint32_t out = 0;
    if (!n) return a;
    for (uint32_t c = 0; c < 3u; c++) {
        const uint32_t mask = (1u << width[c]) - 1u, ca = (a >> shift[c]) & mask, cb = (b >> shift[c]) & mask;
#if BUG == 1
        out |= (((ca * (n - t) + cb * t + n / 2u) / n) << shift[c]);                  /* rounds to nearest: one step off in half of the cases */
#else
        out |= (((ca * (n - t) + cb * t) / n) << shift[c]);
#endif
    }
    return (uint16_t)out;
}
#endif
