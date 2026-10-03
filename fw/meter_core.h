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

/* ---- Colour -----------------------------------------------------------------------------------------------------------------
 * RGB565 blends, per channel so a ramp keeps its hue instead of sliding through grey. Replaces the per-meter copies (ui_mix call
 * sites, Layered Wave's own mixer). Inputs are theme roles or accents; a meter never hard-codes an RGB value for a role.
 * mtr_ramp:   a to b at step t of n (t 0..n), integer division, the exact arithmetic ui_mix always used.
 * mtr_mix256: a to b at weight t256 of 256 (Q8), floor shift, the exact arithmetic Layered Wave's lw_mix used (a different
 *             rounding from mtr_ramp, kept as is so existing frames do not change).
 * mtr_ladder: three-stop ramp lo -> mid -> hi across n steps (the ok/warn/danger ladder the spectrum LEDs draw), step r of n. */
/* noinline on purpose: one shared copy (~112 B) instead of one per call site; it is the same single copy ui_mix always was. */
__attribute__((noinline)) static uint16_t mtr_ramp(uint16_t a, uint16_t b, uint32_t t, uint32_t n)
{
    uint32_t r = (((a >> 11) & 0x1Fu) * (n - t) + ((b >> 11) & 0x1Fu) * t) / n;
    uint32_t g = (((a >> 5)  & 0x3Fu) * (n - t) + ((b >> 5)  & 0x3Fu) * t) / n;
    uint32_t bl = ((a & 0x1Fu) * (n - t) + (b & 0x1Fu) * t) / n;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
static inline uint16_t mtr_mix256(uint16_t a, uint16_t b, int32_t t256)
{
    const int32_t ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    return (uint16_t)(((ar + (((br - ar) * t256) >> 8)) << 11) | ((ag + (((bg - ag) * t256) >> 8)) << 5) | (ab + (((bb - ab) * t256) >> 8)));
}
static inline uint16_t mtr_ladder(uint16_t lo, uint16_t mid, uint16_t hi, uint32_t r, uint32_t n)
{
    const uint32_t half = n / 2u;
    return (r < half) ? mtr_ramp(lo, mid, r, half) : mtr_ramp(mid, hi, r - half, n - half);
}

/* ---- Geometry ---------------------------------------------------------------------------------------------------------------
 * The column and scaling arithmetic every column-based meter wrote for itself (Bars, Dots, the waveform, both scopes, Waterfall,
 * Scroll), with the exact integer rounding they used, so frames do not change.
 * mtr_col_span:  column i of n across [x0, x0+w): its left edge and its lit width (the span minus `gap`, never below 1).
 * mtr_col_cw:    the same column without a gap: its left edge and its full width (never below 1).
 * mtr_scale_u:   value v of `full` mapped onto 0..h, clamped to h.
 * mtr_scale_s:   signed value v of +-unit mapped onto +-ey, clamped to +-ey.
 * mtr_in_box:    1 if a sz x sz square at (px, py) lies wholly inside the rectangle (x, y, w, h). */
#define MTR_AI __attribute__((always_inline)) static inline
MTR_AI void mtr_col_span(uint32_t x0, uint32_t w, uint32_t n, uint32_t i, uint32_t gap, uint32_t *x, uint32_t *lit)
{
    const uint32_t a = x0 + (i * w) / n, b = x0 + ((i + 1u) * w) / n;
    *x = a;
    *lit = (b - a > gap) ? (b - a - gap) : 1u;
}
MTR_AI void mtr_col_cw(uint32_t x0, uint32_t w, uint32_t n, uint32_t i, uint32_t *x, uint32_t *cw)
{
    const uint32_t a = x0 + (i * w) / n, b = x0 + ((i + 1u) * w) / n;
    *x = a;
    *cw = (b > a) ? (b - a) : 1u;
}
MTR_AI uint32_t mtr_scale_u(uint32_t v, uint32_t h, uint32_t full)
{
    uint32_t a = (v * h) / full;
    return a > h ? h : a;
}
MTR_AI int32_t mtr_scale_s(int32_t v, int32_t ey, int32_t unit)
{
    int32_t r = (v * ey) / unit;
    if (r >  ey) r =  ey;
    if (r < -ey) r = -ey;
    return r;
}
MTR_AI int mtr_in_box(int32_t px, int32_t py, int32_t sz, int32_t x, int32_t y, int32_t w, int32_t h)
{
    return px >= x && px + sz <= x + w && py >= y && py + sz <= y + h;
}

/* One-value form of mtr_delta for a cache that tracks a single number per column (Peak Dots, the spectrum LEDs). */
static inline int mtr_delta1(uint8_t *drawn, uint8_t v, uint8_t force)
{
    if (!force && v == *drawn) return 0;
    *drawn = v;
    return 1;
}
/* Invalidate a redraw cache: 0xFF is the "stale" marker. No meter ever stores 0xFF (heights, rows and peaks are all below it), so a stale
 * cell never equals a new value and mtr_delta / mtr_delta1 redraw it without needing `force`. Hosts call this when a context change
 * leaves the pixels under a meter stale (menu closed, fullscreen left, pause resumed); it replaces six hand-written reset loops. */
#define MTR_STALE 0xFFu
static inline void mtr_invalidate(uint8_t *a, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) a[i] = MTR_STALE;
}

/* ---- Signals ----------------------------------------------------------------------------------------------------------------
 * Measurements of the audio a meter may use, written once. The cheap stateless ones are also computed by the host once per frame and
 * handed over in mtr_in_t (energy, silent); the stateful filters below are library functions: the meter owns the small state array
 * and passes its own rates, so each meter keeps exactly the response it was tuned with (the same input can be smoothed several ways).
 *   mtr_energy:   mean band level, 0..255 (n bands of 0..255).
 *   mtr_silent:   1 only for digital silence: zero peak and every band zero.
 *   mtr_slew_pow: slow band weights. Each weight chases the band's power (level squared >> 4, Q12) but moves at most `up` per call
 *                 upward and `dn` downward, so a fast signal cannot flip the picture (Chladni's mode weights).
 *   mtr_ema_pow:  slow band power average: e += (level squared - e) / div, symmetric first order (Layered Wave's energy split).
 *   mtr_onset_flux: spectral-rise trigger. rise = sum of the bands that grew since `prev`; fires when it exceeds a threshold that
 *                 follows its own running average *ema_q8 (sens_q4 is the gain, Q4) and `elapsed` is past the refractory time.
 *                 Updates *ema_q8; the caller owns `prev`, the timestamps and what a trigger means. */
static inline uint32_t mtr_energy(const uint8_t *lvl, uint32_t n)
{
    uint32_t sum = 0;
    for (uint32_t b = 0; b < n; b++) sum += lvl[b];
    return sum / n;
}
static inline uint8_t mtr_silent(const uint8_t *lvl, uint32_t n, uint32_t peak)
{
    if (peak) return 0u;
    for (uint32_t b = 0; b < n; b++) if (lvl[b]) return 0u;
    return 1u;
}
static inline void mtr_slew_pow(int32_t *w, const uint8_t *lvl, uint32_t n, int32_t up, int32_t dn)
{
    for (uint32_t b = 0; b < n; b++) {
        int32_t t = ((int32_t)lvl[b] * lvl[b]) >> 4;
        int32_t d = t - w[b];
        if (d > up) d = up;
        if (d < -dn) d = -dn;
        w[b] += d;
    }
}
static inline void mtr_ema_pow(int32_t *e, const uint8_t *lvl, uint32_t n, int32_t div)
{
    for (uint32_t b = 0; b < n; b++) { const int32_t sq = (int32_t)lvl[b] * lvl[b]; e[b] += (sq - e[b]) / div; }
}
static inline int mtr_onset_flux(const uint8_t *prev, uint32_t *ema_q8, const uint8_t *lvl, uint32_t n, uint32_t sens_q4, uint32_t elapsed, uint32_t refr_ms)
{
    uint32_t rise = 0;
    for (uint32_t b = 0; b < n; b++) if (lvl[b] > prev[b]) rise += lvl[b] - prev[b];
    const uint32_t thr = ((sens_q4 * *ema_q8) >> 12) + 6u;
    const int fire = rise > thr && elapsed > refr_ms;
    *ema_q8 += (int32_t)((rise << 8) - *ema_q8) >> 5;
    return fire;
}
#endif
