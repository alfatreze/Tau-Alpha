/* fw2/cores/spectrum_core.h -- the spectrum meter's arithmetic (the hardware filter bank gives 16 window means; this turns them into bar levels). Pure, host-tested (sim/test_p2_spectrum.py). Calibration and scale are the old
 * player's, measured on hardware (fw/player.c: per-band gains so a typical track lands near 150 of 255, a logarithmic scale of 16 units per octave over a 50-unit (about 19 dB) window above a floor). */
#ifndef SPECTRUM_CORE_H
#define SPECTRUM_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define SPEC_BANDS 16u
#define SPEC_FLOOR 200u
#define SPEC_SPAN  50u
static const uint16_t spec_gain[SPEC_BANDS] = { 1428u, 411u, 788u, 266u, 506u, 228u, 411u, 255u, 430u, 345u, 521u, 496u, 699u, 764u, 1010u, 1312u };

/* 16 units per octave: the exponent in the high bits, the next four mantissa bits below it. */
static inline uint32_t spec_log(uint32_t v)
{
    if (!v) return 0;
    uint32_t e = 0u, t = v;
    while (t > 1u) { t >>= 1; e++; }
    uint32_t m = (e >= 4u) ? ((v >> (e - 4u)) & 0xFu) : ((v << (4u - e)) & 0xFu);
    return (e << 4) | m;
}
/* The 0..255 level of band `b` (0 = lowest; the bank's means are scaled by the gain of the mirrored band, as the old player did). */
static inline uint32_t spec_level(uint32_t mean, uint32_t b)
{
#if BUG == 1
    uint32_t v = (mean * spec_gain[b]) >> 4;                                  /* the gain table is not mirrored */
#else
    uint32_t v = (mean * spec_gain[SPEC_BANDS - 1u - b]) >> 4;
#endif
    uint32_t lg = spec_log(v);
    v = lg > SPEC_FLOOR ? ((lg - SPEC_FLOOR) * 255u) / SPEC_SPAN : 0u;
    return v > 255u ? 255u : v;
}
/* Instant attack, smooth release (a quarter of the way down plus one per call). */
static inline uint32_t spec_ballistics(uint32_t cur, uint32_t v)
{
#if BUG == 2
    return v;                                                                 /* no release smoothing: the bars flicker */
#else
    if (v >= cur) return v;
    return cur - ((cur - v) / 4u + 1u);
#endif
}
#endif
