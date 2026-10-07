/* fw/halcyon_core.h -- the Halcyon macro layer, portable part (B-631, parallel plan A5; docs/features/CYMO_HALCYON_SPEC.md). No MMIO, no libc, no trigonometry: host-tested against tools/lab/halcyon_model.py.
 *
 * Six control positions (warmth, bass, vocal, punch, sibilance, air) become six stage gains in half-dB steps (the integer form of halcyon_model.stage_gains: every product with 0.9 or
 * 0.8 is a rounded multiple of 0.1 dB, so it is exact in tenths and no rounding tie can occur), each gain selects a row of the generated tables (fw/halcyon_tab.h: Q2.22 coefficients of the
 * analog-matched design), and the peak-safe preamp comes from adding the stages' magnitude rows in dB on a 40-point log grid (cascade magnitudes add in dB exactly): the largest sum is the
 * boost to give back, rounded UP to 1/8 dB plus a 0.25 dB margin for the points between grid frequencies. Attenuate only: with no boost the preamp is unity.
 * Nothing here writes hardware: the writable coefficient store of the Halcyon RTL does not exist yet. */
#ifndef TAU_HALCYON_CORE_H
#define TAU_HALCYON_CORE_H
#include <stdint.h>
#include "halcyon_tab.h"

typedef struct { int8_t warmth, bass, vocal, punch, sibilance, air; } hal_ctl_t;

#define HAL_PRE_MARGIN_EIGHTHS 2     /* 0.25 dB */

static inline int32_t hal_rdiv(int32_t n, int32_t d) { return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d); }
static inline int32_t hal_clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Table row (0..36) of each stage for a control setting. Positions are clamped to their ranges (sibilance 0..5, the rest -5..+5). */
static inline void hal_steps(const hal_ctl_t *c, uint8_t step[HAL_NSTAGE])
{
    const int32_t w = hal_clampi(c->warmth, -5, 5), b = hal_clampi(c->bass, -5, 5), v = hal_clampi(c->vocal, -5, 5),
                  p = hal_clampi(c->punch, -5, 5), s = hal_clampi(c->sibilance, 0, 5), a = hal_clampi(c->air, -5, 5);
    const int32_t h[HAL_NSTAGE] = {
        2 * b + w,                        /* low shelf: bass weight plus the warm side of the tilt (1.0 b + 0.5 w, in half dB) */
        hal_rdiv(16 * w, 10),             /* body 0.8 w */
        hal_rdiv(18 * v, 10),             /* vocal 0.9 v */
        hal_rdiv(18 * p, 10),             /* punch 0.9 p */
        -2 * s,                           /* sibilance: dip only */
        hal_rdiv(18 * a - 10 * w, 10),    /* air 0.9 a minus the clear side of the tilt 0.5 w */
    };
    for (uint32_t i = 0; i < HAL_NSTAGE; i++) step[i] = (uint8_t)(hal_clampi(h[i], -18, 18) + 18);
}

/* Largest boost of the cascade on the grid, in 1/64 dB (can be negative when everything cuts). */
static inline int32_t hal_peak_db64(const uint8_t step[HAL_NSTAGE])
{
    int32_t best = -32768;
    for (uint32_t k = 0; k < HAL_NGRID; k++) {
        int32_t sum = 0;
        for (uint32_t s = 0; s < HAL_NSTAGE; s++) sum += hal_mag[s][step[s]][k];
        if (sum > best) best = sum;
    }
    return best;
}

/* Attenuation to apply, in 1/8 dB (0 = unity), and the preamp as a Q2.22 factor. */
static inline int32_t hal_atten_eighths(const uint8_t step[HAL_NSTAGE])
{
    const int32_t pk = hal_peak_db64(step);
    if (pk <= 0) return 0;
    int32_t n = (pk + 7) / 8 + HAL_PRE_MARGIN_EIGHTHS;       /* ceil(pk / 8): 1/64 dB to 1/8 dB, rounded up */
    return n > 512 ? 512 : n;
}
static inline int32_t hal_preamp_q22(int32_t eighths) { return hal_pre[eighths]; }
#endif
