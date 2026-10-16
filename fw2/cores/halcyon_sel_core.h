/* fw2/cores/halcyon_sel_core.h -- which Halcyon preset is selected, and how the keys and the saved value move it (the rules of the previous player's fw/halcyon.inc and fw/halcyon_page.inc, as pure
 * arithmetic). Selection: 0 = OFF, 1..8 the built-in presets, 9.. the user presets of the PRST section, 200 = CUSTOM (the six sliders of the page are the user's own, not a preset). The saved value
 * (persist id 16, 0..30) is never CUSTOM: CUSTOM keeps the last preset on file. Host-tested (sim/test_p2_halcyon_sel.py). */
#ifndef HALCYON_SEL_CORE_H
#define HALCYON_SEL_CORE_H
#include <stdint.h>
#include "helios_core.h"
#ifndef BUG
#define BUG 0
#endif
#define HS_CUSTOM 200u
/* Y on Now Playing: the next preset; after the last, back to OFF. From CUSTOM the cycle restarts at OFF. n = built-in + user presets. */
static inline uint32_t hs_cycle(uint32_t sel, uint32_t n)
{
    if (sel >= HS_CUSTOM) sel = 0u;
#if BUG == 1
    return (sel + 1u) % n;                                       /* OFF is skipped */
#else
    return (sel + 1u) % (n + 1u);
#endif
}
/* L1/R1 on the Halcyon page: the next or previous PRESET, never OFF; from OFF or CUSTOM, the first (next) or the last (previous). */
static inline uint32_t hs_step(uint32_t sel, uint32_t n, int dir)
{
    uint32_t cur = (sel >= 1u && sel <= n) ? sel : 0u;
    return dir > 0 ? (cur % n) + 1u : (cur <= 1u ? n : cur - 1u);
}
/* A saved or Core Settings value arrives: anything that is not a preset this build has means OFF. */
static inline uint32_t hs_restore(uint32_t v, uint32_t n)
{
#if BUG == 2
    (void)n; return v;                                           /* a stale value from a build with more presets goes through */
#else
    return (v > n || v >= HS_CUSTOM) ? 0u : v;
#endif
}
/* What is written to the saved word: the selection, but CUSTOM keeps what was on file. */
static inline uint32_t hs_saved(uint32_t sel, uint32_t on_file)
{
#if BUG == 3
    (void)on_file; return sel > 30u ? 30u : sel;                 /* CUSTOM clamps to 30 and overwrites the preset on file */
#else
    return sel < 31u ? sel : on_file;
#endif
}
/* The page: row 0 is the preset area, rows 1..6 the six controls (warmth, bass, vocal, punch, sibilance, air). */
static inline uint32_t hs_row(uint32_t row, int up) { return hl_wrap(row, 7u, up); }
static inline int32_t hs_ctl_clamp(uint32_t row, int32_t v)
{
    const int32_t lo = row == 5u ? 0 : -5;                       /* row 5 = sibilance: a dip depth, 0..5 */
    return v < lo ? lo : v > 5 ? 5 : v;
}
#endif
