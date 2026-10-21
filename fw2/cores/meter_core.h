/* Shared meter core, tranche 1 (docs/METER_MODULE_SPEC.md sections 13 and 17, M1.5): the transversal functions that were written
 * inside the Winamp meters and are needed by the next meter. Portable: no MMIO, no engine calls, no float, no player.c state
 * (tools/check_meter_deps.py enforces it), so it is tested natively (sim/test_meter_core.py) and has a JS twin
 * (tools/meters/preview/tau_core.js) checked against the same vectors.
 *
 * These are the exact functions and arithmetic that fw/player.c's wviz_bars_tick() carried; moving them here changed no behaviour
 * (sim/test_meter_core.py compares them with the original code over exhaustive and randomised inputs).
 * Rule 3 of the spec: a function enters the core when a second meter needs it; the Winamp pair is the first user, the first
 * additional user of each is listed below. */
#ifndef TAU_METER_CORE_H
#define TAU_METER_CORE_H
#include <stdint.h>

/* ---- Ballistics ------------------------------------------------------------------------------------------------------------
 * One value toward one target by one of four curves: 0 instant, 1 linear, 2 exponential, 3 spring. `rate` is 1..100 (attack or
 * release, whichever applies this tick); `*vel` is touched only by the spring mode and must be one slot per band across calls.
 * Integer fixed point (the core has no hardware float). Next users: VIZ_BARS, VIZ_LED, VIZ_LEVELS' fall arithmetic. */
static inline uint8_t mtr_ease(uint8_t cur, uint8_t target, uint32_t mode, uint32_t rate, int16_t *vel)
{
    if (mode == 0u) return target;                      /* instant */
    if (mode == 1u) {                                    /* linear: fixed step per tick */
        int32_t step = 1 + (int32_t)rate / 6;            /* ~1..17 per ~26 ms tick */
        int32_t d = (int32_t)target - (int32_t)cur;
        if (d > step) d = step; else if (d < -step) d = -step;
        return (uint8_t)((int32_t)cur + d);
    }
    if (mode == 2u) {                                    /* exponential: k in 8..256 (Q8) */
        int32_t k = 8 + ((int32_t)rate * 248) / 100;
        int32_t d = (((int32_t)target - (int32_t)cur) * k) / 256;
        return (uint8_t)((int32_t)cur + d);
    }
    {                                                    /* spring: damping tied to stiffness, slightly underdamped on purpose */
        int32_t stiff = 6 + ((int32_t)rate * 58) / 100;
        int32_t accel = (((int32_t)target - (int32_t)cur) * stiff) / 256 - ((int32_t)*vel * stiff) / 512;
        int32_t v = (int32_t)*vel + accel;
        if (v > 60) v = 60; else if (v < -60) v = -60;
        *vel = (int16_t)v;
        int32_t p = (int32_t)cur + v;
        if (p < 0) p = 0; else if (p > 255) p = 255;
        return (uint8_t)p;
    }
}

/* ---- Peak cap ---------------------------------------------------------------------------------------------------------------
 * Hold, then fall linearly or with gravity. One state per band, owned by the module. `on` = 0 makes the cap track the level. */
typedef struct { uint8_t peak, vel; uint16_t hold; } mtr_peak_t;
typedef struct { uint8_t on, gravity; uint16_t hold_ms; uint8_t fall; } mtr_peak_cfg_t;

static inline void mtr_peak_step(mtr_peak_t *p, uint8_t disp, const mtr_peak_cfg_t *c, uint32_t dt_ms)
{
    if (!c->on) { p->peak = disp; return; }
    if (disp >= p->peak) {
        p->peak = disp; p->hold = c->hold_ms; p->vel = 0u;
    } else if (p->hold > 0u) {
        p->hold = (p->hold > dt_ms) ? (uint16_t)(p->hold - dt_ms) : 0u;
    } else {
        uint32_t fall;
        if (c->gravity) {
            uint32_t nv = p->vel + 1u + c->fall / 20u;
            p->vel = (uint8_t)((nv > 255u) ? 255u : nv);
            fall = 1u + (p->vel >> 3);
        } else {
            fall = 1u + c->fall / 12u;
        }
        p->peak = (p->peak > fall) ? (uint8_t)(p->peak - fall) : 0u;
        if (p->peak < disp) p->peak = disp;
    }
}

/* ---- Band mapping -----------------------------------------------------------------------------------------------------------
 * Output band `b` of `bands`, the mean of the source bands that fall in its share of `nspec` (at least one). Next users: Chladni's
 * band-to-mode mapping, VIZ_LED. */
static inline uint32_t mtr_band_target(const uint8_t *spec, uint32_t nspec, uint32_t bands, uint32_t b)
{
    uint32_t lo = (b * nspec) / bands, hi = ((b + 1u) * nspec) / bands;
    if (hi <= lo) hi = lo + 1u;
    uint32_t sum = 0, n = 0;
    for (uint32_t k = lo; k < hi && k < nspec; k++) { sum += spec[k]; n++; }
    return n ? sum / n : 0u;
}

/* ---- Redraw cache -----------------------------------------------------------------------------------------------------------
 * "Did anything change since I last drew this column?" Records the new pair as drawn and returns 1, or returns 0 to skip. `force`
 * (context changed: preset, mode, page) always redraws; this is the one tested implementation of the cache whose ad hoc
 * version broke in B-234. */
static inline int mtr_delta(uint8_t *drawn_a, uint8_t *drawn_b, uint8_t a, uint8_t b, uint8_t force)
{
    if (!force && a == *drawn_a && b == *drawn_b) return 0;
    *drawn_a = a; *drawn_b = b;
    return 1;
}
#endif
