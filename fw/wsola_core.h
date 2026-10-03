/* fw/wsola_core.h -- fixed-point WSOLA tempo core: pitch-preserving speed change for speech (Cymo C7, B-549).
 *
 * Portable and host-testable: no MMIO, no floating point, no libc. int32 arithmetic everywhere except one 64-bit multiply and divide per candidate in the search.
 * The design and the numbers behind it: docs/features/CYMO_AUDIO_ENGINE.md section 7, tools/lab/cymo_tempo_model.py (the float quality reference) and
 * tools/lab/wsola_fixed_ref.py (the independent integer twin this file must match sample for sample; sim/test_wsola.py checks it).
 *
 * How it works. The input is cut into overlapping grains (N = 1024 samples for rates of 32 kHz and up, 512 below: about 23 ms) and re-assembled with a 50% overlap
 * and a Q15 Hann window. The output advances by Hs = N/2 per grain, the input by Hs * speed: that is the speed change, and because every grain is a piece of
 * the original waveform the pitch does not move. Each grain's start is searched, +-10 ms around its nominal position, for the place where it best continues
 * the previous grain (normalised cross-correlation): coarse on a mono mix decimated 8x (4x below 32 kHz), then refined at the full rate. That search is what
 * keeps periodic speech aligned; without it the joins buzz.
 *
 * The caller owns the audio. The core never stores input: before each step it says which absolute sample range it will read (ws_need), the caller makes sure
 * that range is readable through the slab pointers (xl / xr, slab_lo = the absolute index of xl[0]), and ws_step then produces Hs output samples per channel.
 * Between steps the core keeps only the previous grain's second half (Hs samples per channel). Stack use in a step: about 2.3 KB. Speed is limited to 0.5x..3.0x.
 *
 * No 64-bit division: the search compares num*num/en, and ws_divlu() computes that quotient with 32-bit divides only (Hacker's Delight, divlu); the quotient is
 * always below 2^32 here because num*num <= en * (energy of the reference) and the reference energy is below 2^27, so the result is exact. */
#ifndef WSOLA_CORE_H
#define WSOLA_CORE_H
#include <stdint.h>
#include "wsola_tables.h"


#define WS_MAX_N   1024u
#define WS_MAX_HS  (WS_MAX_N / 2u)
#define WS_REF_LEN 256u
#define WS_DELTA_C 55u            /* search radius in coarse samples (about 10 ms) */
#define WS_Q15     32768
#define WS_SPEED_MIN_Q8 128u      /* 0.5x */
#define WS_SPEED_MAX_Q8 768u      /* 3.0x: the largest the fixed scratch below is sized for */

#ifdef WS_COUNT
static uint32_t ws_macs;          /* host tests only: multiply-adds spent in the search */
#endif

typedef struct {
    uint32_t speed_q8;            /* input advance per output hop, Q8 (1.5x = 384) */
    uint64_t p_q8;                /* nominal start of the next grain in the input, Q8 */
    uint32_t prev;                /* absolute start of the previously chosen grain */
    uint32_t k;                   /* grains produced */
    uint16_t N, Hs, nc, delta;    /* grain, hop, coarse grain length, search radius in samples */
    uint8_t  dec, shift, ch;      /* coarse decimation, its log2, channels (1 or 2) */
    int16_t  pend[2][WS_MAX_HS];  /* second half of the previous grain (raw) */
} ws_t;

static inline void ws_init(ws_t *s, uint32_t fs, uint8_t ch, uint32_t speed_q8)
{
    s->ch = ch;
    if (speed_q8 < WS_SPEED_MIN_Q8) speed_q8 = WS_SPEED_MIN_Q8;
    if (speed_q8 > WS_SPEED_MAX_Q8) speed_q8 = WS_SPEED_MAX_Q8;
    s->speed_q8 = speed_q8;
    if (fs >= 32000u) { s->dec = 8u; s->shift = 3u; s->N = 1024u; }
    else              { s->dec = 4u; s->shift = 2u; s->N = 512u; }
    s->Hs = (uint16_t)(s->N / 2u);
    s->nc = (uint16_t)(s->N / s->dec);
    s->delta = (uint16_t)(WS_DELTA_C * s->dec);
    s->k = 0u;
    s->prev = 0u;
    s->p_q8 = (uint64_t)s->Hs * speed_q8;
    for (uint32_t c = 0; c < 2u; c++) for (uint32_t i = 0; i < WS_MAX_HS; i++) s->pend[c][i] = 0;
}

/* the two ends of the search in coarse samples, and the reference position */
static inline void ws_geom(const ws_t *s, uint32_t *tgt, uint32_t *p, uint32_t *lo_c, uint32_t *hi_c)
{
    *tgt = s->prev + s->Hs;
    *p = (uint32_t)((s->p_q8 + 128u) >> 8);
    *lo_c = (*p > s->delta) ? (*p - s->delta) / s->dec : 0u;
    *hi_c = (*p + s->delta) / s->dec;
}

/* The absolute sample range [lo, hi) the next ws_step reads. */
static inline void ws_need(const ws_t *s, uint32_t *lo, uint32_t *hi)
{
    if (s->k == 0u) { *lo = 0u; *hi = s->N; return; }
    uint32_t tgt, p, lo_c, hi_c;
    ws_geom(s, &tgt, &p, &lo_c, &hi_c);
    const uint32_t lo_a = lo_c ? lo_c * s->dec - s->dec : 0u;
    const uint32_t l0 = (tgt / s->dec) * s->dec;
    *lo = l0 < lo_a ? l0 : lo_a;
    const uint32_t h0 = tgt + s->N, h1 = hi_c * s->dec + s->dec + s->N;
    *hi = h0 > h1 ? h0 : h1;
}

static inline int32_t ws_mix(const ws_t *s, const int16_t *xl, const int16_t *xr, uint32_t i)
{
    return s->ch == 2u ? ((int32_t)xl[i] + (int32_t)xr[i]) >> 1 : (int32_t)xl[i];
}

static inline uint32_t ws_scale(const int16_t *v, uint32_t n)
{
    int32_t m = 0;
    for (uint32_t i = 0; i < n; i++) { int32_t a = v[i] < 0 ? -(int32_t)v[i] : v[i]; if (a > m) m = a; }
    uint32_t sc = 0;
    while ((m >> sc) > 1023) sc++;
    return sc;
}

static inline int32_t ws_clamp2047(int32_t v) { return v > 2047 ? 2047 : (v < -2047 ? -2047 : v); }

static inline uint32_t ws_nlz(uint32_t x)
{
    uint32_t n = 0;
    if (!(x & 0xFFFF0000u)) { n += 16u; x <<= 16; }
    if (!(x & 0xFF000000u)) { n += 8u;  x <<= 8; }
    if (!(x & 0xF0000000u)) { n += 4u;  x <<= 4; }
    if (!(x & 0xC0000000u)) { n += 2u;  x <<= 2; }
    if (!(x & 0x80000000u)) { n += 1u; }
    return n;
}

/* floor((u1 * 2^32 + u0) / v) for u1 < v and v > 0, from 32-bit operations only (Hacker's Delight, divlu). */
static inline uint32_t ws_divlu(uint32_t u1, uint32_t u0, uint32_t v)
{
    const uint32_t b = 65536u;
    const uint32_t s = ws_nlz(v);
    v <<= s;
    const uint32_t vn1 = v >> 16, vn0 = v & 0xFFFFu;
    const uint32_t un32 = s ? ((u1 << s) | (u0 >> (32u - s))) : u1;
    const uint32_t un10 = u0 << s;
    const uint32_t un1 = un10 >> 16, un0 = un10 & 0xFFFFu;
    uint32_t q1 = un32 / vn1, rhat = un32 - q1 * vn1;
    while (q1 >= b || q1 * vn0 > b * rhat + un1) { q1--; rhat += vn1; if (rhat >= b) break; }
    const uint32_t un21 = un32 * b + un1 - q1 * v;
    uint32_t q0 = un21 / vn1;
    rhat = un21 - q0 * vn1;
    while (q0 >= b || q0 * vn0 > b * rhat + un0) { q0--; rhat += vn1; if (rhat >= b) break; }
    return q1 * b + q0;
}

/* Best start in cand[0 .. ncand) for `ref` (length n); cand has ncand + n - 1 entries. Returns the index, or -1 if nothing correlates positively. */
static inline int32_t ws_search(const int16_t *ref, const int16_t *cand, uint32_t ncand, uint32_t n)
{
    int32_t refen = 0;
    for (uint32_t i = 0; i < n; i++) refen += (int32_t)ref[i] * ref[i];
    if (refen == 0) return -1;
    int32_t en = 0;
    for (uint32_t i = 0; i < n; i++) en += (int32_t)cand[i] * cand[i];
    int32_t best = -1;
    int64_t best_s = -1;                      /* scores are below 2^32; -1 means none yet */
    for (uint32_t c = 0; c < ncand; c++) {
        if (c) { const int32_t o = cand[c - 1], nw = cand[c + n - 1]; en += nw * nw - o * o; }
        /* the dot product, four terms a pass (n is a multiple of 4): integer addition is associative, so the result is the same as the plain loop and the
         * loop overhead per multiply-add drops from about three instructions to under one */
        int32_t num = 0;
        const int16_t *a = ref, *b = cand + c;
        for (uint32_t i = 0; i < n; i += 4u, a += 4, b += 4)
            num += (int32_t)a[0] * b[0] + (int32_t)a[1] * b[1] + (int32_t)a[2] * b[2] + (int32_t)a[3] * b[3];
#ifdef WS_COUNT
        ws_macs += n;
#endif
        if (num > 0) {
            const uint64_t sq = (uint64_t)((int64_t)num * num);
            const uint32_t den = (uint32_t)(en > 0 ? en : 1);
            /* floor(sq / den) > best_s  <=>  sq >= (best_s + 1) * den: an exact test that needs no division, so the divide runs only when a candidate improves */
            if (best_s < 0 || sq >= (uint64_t)(best_s + 1) * den) {
                const int64_t score = (int64_t)ws_divlu((uint32_t)(sq >> 32), (uint32_t)sq, den);
                if (score > best_s) { best_s = score; best = (int32_t)c; }
            }
        } else if (best_s < 0) { best_s = 0; best = (int32_t)c; }   /* a non-positive correlation scores 0 and only ever counts as the first candidate */
    }
    return best_s > 0 ? best : -1;
}

/* Produce one hop: Hs samples per channel into outl (and outr when stereo). xl/xr: slab pointers, slab_lo: absolute index of xl[0]; the range from ws_need must lie
 * inside the slab. Returns Hs. */
static inline uint32_t ws_step(ws_t *s, const int16_t *xl, const int16_t *xr, uint32_t slab_lo, int16_t *outl, int16_t *outr)
{
    const uint32_t N = s->N, Hs = s->Hs, dec = s->dec;
    int16_t *outs[2] = { outl, outr };
    const int16_t *xs[2] = { xl, xr };
    if (s->k == 0u) {
        for (uint32_t c = 0; c < s->ch; c++) {
            const int16_t *g0 = xs[c] - slab_lo;                       /* absolute indexing, as below */
            for (uint32_t i = 0; i < Hs; i++) { outs[c][i] = g0[i]; s->pend[c][i] = g0[Hs + i]; }
        }
        s->prev = 0u;
        s->k = 1u;
        return Hs;
    }
    uint32_t tgt, p, lo_c, hi_c;
    ws_geom(s, &tgt, &p, &lo_c, &hi_c);
    /* slab-relative views: index with absolute sample numbers */
    const int16_t *l = xl - slab_lo;
    const int16_t *r = xr ? xr - slab_lo : (const int16_t *)0;

    /* coarse search: decimated mono mix over the reference and every candidate window */
    const uint32_t tj = tgt / dec;
    const uint32_t j0 = lo_c < tj ? lo_c : tj;
    const uint32_t j1 = (hi_c + s->nc) > (tj + s->nc) ? (hi_c + s->nc) : (tj + s->nc);
    int16_t xd[448];     /* decimated samples j0 .. j1-1: 111 candidate starts + 127 window samples, plus up to 184 more when the reference sits far from the search window (the previous grain landed up to a search radius off its nominal place, and at 3.0x the nominal step is two hops further than the reference) */
    for (uint32_t j = j0; j < j1; j++) {
        int32_t acc = 0;
        for (uint32_t i = 0; i < dec; i++) acc += ws_mix(s, l, r, j * dec + i);
        xd[j - j0] = (int16_t)(acc >> s->shift);
    }
    int16_t ref[128];
    for (uint32_t i = 0; i < s->nc; i++) ref[i] = xd[tj + i - j0];
    const uint32_t sc = ws_scale(ref, s->nc);
    for (uint32_t i = 0; i < s->nc; i++) ref[i] = (int16_t)(ref[i] >> sc);
    const uint32_t ncand = hi_c - lo_c + 1u;
    int16_t cand[(WS_DELTA_C * 2u + 1u) + 128u];
    for (uint32_t i = 0; i < ncand + s->nc - 1u; i++) cand[i] = (int16_t)ws_clamp2047((int32_t)xd[lo_c + i - j0] >> sc);
    const int32_t idx = ws_search(ref, cand, ncand, s->nc);
    uint32_t cd;
    if (idx >= 0) cd = lo_c + (uint32_t)idx;
    else { cd = p / dec; if (cd < lo_c) cd = lo_c; if (cd > hi_c) cd = hi_c; }

    /* refine at the full rate over +-dec samples */
    const uint32_t c0 = cd * dec;
    const uint32_t rlo = c0 > dec ? c0 - dec : 0u;
    const uint32_t nref = c0 + dec - rlo + 1u;
    int16_t mixr[WS_REF_LEN + 2u * 8u + 1u];
    for (uint32_t j = 0; j < nref + WS_REF_LEN - 1u; j++) mixr[j] = (int16_t)ws_mix(s, l, r, rlo + j);
    int16_t rf[WS_REF_LEN];
    for (uint32_t i = 0; i < WS_REF_LEN; i++) rf[i] = (int16_t)ws_mix(s, l, r, tgt + i);
    const uint32_t s2 = ws_scale(rf, WS_REF_LEN);
    for (uint32_t i = 0; i < WS_REF_LEN; i++) rf[i] = (int16_t)(rf[i] >> s2);
    for (uint32_t j = 0; j < nref + WS_REF_LEN - 1u; j++) mixr[j] = (int16_t)ws_clamp2047((int32_t)mixr[j] >> s2);
    const int32_t idx2 = ws_search(rf, mixr, nref, WS_REF_LEN);
    const uint32_t c = idx2 >= 0 ? rlo + (uint32_t)idx2 : c0;

    /* overlap-add the chosen grain; keep its second half for the next hop */
    const uint32_t stp = (N == 1024u) ? 1u : 2u;
    for (uint32_t ch = 0; ch < s->ch; ch++) {
        const int16_t *g = (ch ? r : l) + c;
        for (uint32_t i = 0; i < Hs; i++) {
            const int32_t w = ws_hann_q15[i * stp];
            const int32_t v = ((int32_t)s->pend[ch][i] * (WS_Q15 - w) + (int32_t)g[i] * w + 16384) >> 15;
            outs[ch][i] = (int16_t)v;
            s->pend[ch][i] = g[Hs + i];
        }
    }
    s->prev = c;
    s->p_q8 += (uint64_t)Hs * s->speed_q8;
    s->k++;
    return Hs;
}
#endif
