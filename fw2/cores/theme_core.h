/* fw2/cores/theme_core.h -- the arithmetic of the Player 2 themes (docs/THEME_SPEC.md): the accent the user picked from the 19 Analogue Pocket colours, as it is shown on a dark or a light theme, and the wrap
 * rule of the colour pick. Pure: host-tested (sim/test_p2_theme.py) against an independent statement. In the Light polarity a bright pick would vanish on a light ramp, so it is scaled down to TH_LIGHT_ACC_MAX_L
 * luma (hue kept); tools/gen_themes.py mirrors this to check contrast. */
#ifndef THEME_CORE_H
#define THEME_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define TC_PALETTE_N 19u
#define TC_LIGHT_ACC_MAX_L 110u
static inline uint16_t tc_accent(uint16_t a, uint32_t light)
{
    if (!light) return a;
    uint32_t r = ((a >> 11) & 0x1Fu) * 255u / 31u, g = ((a >> 5) & 0x3Fu) * 255u / 63u, b = (a & 0x1Fu) * 255u / 31u;
    uint32_t l = (2126u * r + 7152u * g + 722u * b) / 10000u;
#if BUG == 1
    if (l >= TC_LIGHT_ACC_MAX_L + 40u) {                                      /* too late: bright accents survive */
#else
    if (l > TC_LIGHT_ACC_MAX_L) {
#endif
        r = r * TC_LIGHT_ACC_MAX_L / l; g = g * TC_LIGHT_ACC_MAX_L / l; b = b * TC_LIGHT_ACC_MAX_L / l;
        a = (uint16_t)((((r * 31u + 127u) / 255u) << 11) | (((g * 63u + 127u) / 255u) << 5) | ((b * 31u + 127u) / 255u));
    }
    return a;
}
/* The colour pick moved by one (L1/R1): wraps over the palette. */
static inline uint32_t tc_pick_step(uint32_t idx, int dir)
{
#if BUG == 2
    return dir > 0 ? (idx + 1u < TC_PALETTE_N ? idx + 1u : TC_PALETTE_N - 1u) : (idx ? idx - 1u : 0u);   /* clamps */
#else
    return dir > 0 ? (idx + 1u < TC_PALETTE_N ? idx + 1u : 0u) : (idx ? idx - 1u : TC_PALETTE_N - 1u);
#endif
}
/* A saved index that is not a palette entry, theme or mode of this build falls back to the default. */
static inline uint32_t tc_valid(uint32_t v, uint32_t n, uint32_t def) { return v < n ? v : def; }
#endif
