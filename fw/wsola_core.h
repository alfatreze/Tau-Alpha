/* fw/wsola_core.h -- fixed-point WSOLA tempo core: pitch-preserving speed change for speech (Cymo C7, B-549).
 *
 * Portable and host-testable: no MMIO, no floating point, no libc. int32 arithmetic everywhere except one 64-bit multiply and divide per candidate in the search.
 * The design and the numbers behind it: docs/features/CYMO_AUDIO_ENGINE.md section 7, tools/lab/cymo_tempo_model.py (the float quality reference) and
 * tools/lab/wsola_fixed_ref.py (the independent integer twin this file must match sample for sample; sim/test_wsola.py checks it).
 *
 * How it works. The input is cut into overlapping grains (N = 1024 samples for rates of 32 kHz and up, 512 below: about 23 ms) and re-assembled with a 50% overlap
 * and a Q15 Hann window. The output advances by Hs = N/2 per grain, the input by Hs * speed: that is the speed change, and because every grain is a piece of
 * the original waveform the pitch does not move. Each grain's start is searched, +-10 ms around its nominal position, for the place where it best continues
 * the previous grain (normalised cross-correlation) in three stages, each narrowing the last: +-14 candidates on a mono mix decimated 32x (16x below 32 kHz), +-4
 * on the same mix decimated 8x (4x), then +-8 samples (+-4) at the full rate. That search is what keeps periodic speech aligned; without it the joins buzz. The
 * three-stage form costs about a quarter of the multiply-adds of a single coarse stage with no loss on speech (tools/lab/wsola_search_lab.py, B-550).
 *
 * The caller owns the audio. The core never stores input: before each step it says which absolute sample range it will read (ws_need), the caller makes sure
 * that range is readable through the slab pointers (xl / xr, slab_lo = the absolute index of xl[0]), and ws_step then produces Hs output samples per channel.
 * Between steps the core keeps only the previous grain's second half (Hs samples per channel). Stack use in a step: about 2.1 KB. Speed is limited to 0.5x..3.0x.
 *
 * No 64-bit division: the search compares num*num/en, and ws_divlu() computes that quotient with 32-bit divides only (Hacker's Delight, divlu); the quotient is
 * always below 2^32 here because num*num <= en * (energy of the reference) and the reference energy is below 2^27, so the result is exact. */
#ifndef WSOLA_CORE_H
#define WSOLA_CORE_H
#include <stdint.h>
#include "wsola_tables.h"


#define WS_MAX_N   1024u
#define WS_MAX_HS  (WS_MAX_N / 2u)
#define WS_REF_LEN 128u           /* the fine stage's window */
#define WS_R1      14u            /* stage-1 radius in candidates (about 10 ms) */
#define WS_R2      4u             /* stage-2 radius in candidates */
#define WS_Q15     32768
#define WS_SPEED_MIN_Q8 128u      /* 0.5x */
#define WS_SPEED_MAX_Q8 768u      /* 3.0x: the largest the fixed scratch below is sized for */

/* WS_FN: the linkage/placement of the core's functions. The default is `static inline`; the firmware defines it to put them in cold code (PSRAM, fw/player.c). */
#ifndef WS_FN
#define WS_FN static inline
#endif

#ifdef WS_COUNT
static uint32_t ws_macs;          /* host tests only: multiply-adds spent in the search */
#endif

typedef struct {
    uint32_t speed_q8;            /* input advance per output hop, Q8 (1.5x = 384) */
    uint64_t p_q8;                /* nominal start of the next grain in the input, Q8 */
    uint32_t prev;                /* absolute start of the previously chosen grain */
    uint32_t k;                   /* grains produced */
    uint16_t N, Hs, nc1, nc2;     /* grain, hop, the stage-1 and stage-2 window lengths (N / D1, N / D2) */
    uint8_t  D1, D2, shift, ch;   /* the two decimations (32 and 8; 16 and 4 below 32 kHz), log2(D2), channels (1 or 2) */
    int16_t  pend[2][WS_MAX_HS];  /* second half of the previous grain (raw) */
} ws_t;

WS_FN void ws_init(ws_t *s, uint32_t fs, uint8_t ch, uint32_t speed_q8)
{
    s->ch = ch;
    if (speed_q8 < WS_SPEED_MIN_Q8) speed_q8 = WS_SPEED_MIN_Q8;
    if (speed_q8 > WS_SPEED_MAX_Q8) speed_q8 = WS_SPEED_MAX_Q8;
    s->speed_q8 = speed_q8;
    if (fs >= 32000u) { s->D1 = 32u; s->D2 = 8u; s->shift = 3u; s->N = 1024u; }
    else              { s->D1 = 16u; s->D2 = 4u; s->shift = 2u; s->N = 512u; }
    s->Hs = (uint16_t)(s->N / 2u);
    s->nc1 = (uint16_t)(s->N / s->D1);
    s->nc2 = (uint16_t)(s->N / s->D2);
    s->k = 0u;
    s->prev = 0u;
    s->p_q8 = (uint64_t)s->Hs * speed_q8;
    for (uint32_t c = 0; c < 2u; c++) for (uint32_t i = 0; i < WS_MAX_HS; i++) s->pend[c][i] = 0;
}

/* Where the next grain is searched: tgt = where the previous grain's tail leads on, p = the nominal start, [lo1, hi1] = the stage-1 candidates (units of D1),
 * [e0, e1) = the stage-2-rate samples the candidate region covers (stage-2 candidates reach 4 beyond stage 1's). */
WS_FN void ws_geom(const ws_t *s, uint32_t *tgt, uint32_t *p, uint32_t *c1, uint32_t *lo1, uint32_t *hi1, uint32_t *e0, uint32_t *e1)
{
    *tgt = s->prev + s->Hs;
    *p = (uint32_t)((s->p_q8 + 128u) >> 8);
    *c1 = *p / s->D1;
    *lo1 = (*c1 > WS_R1) ? *c1 - WS_R1 : 0u;
    *hi1 = *c1 + WS_R1;
    *e0 = (*lo1 * 4u > 4u) ? *lo1 * 4u - 4u : 0u;
    *e1 = *hi1 * 4u + 4u + s->nc2;
}

/* The absolute sample range [lo, hi) the next ws_step reads. */
WS_FN void ws_need(const ws_t *s, uint32_t *lo, uint32_t *hi)
{
    if (s->k == 0u) { *lo = 0u; *hi = s->N; return; }
    uint32_t tgt, p, c1, lo1, hi1, e0, e1;
    ws_geom(s, &tgt, &p, &c1, &lo1, &hi1, &e0, &e1);
    const uint32_t rb = (tgt / s->D1) * s->D1;
    const uint32_t l1 = e0 * s->D2;
    *lo = rb < l1 ? rb : l1;
    uint32_t h = rb + (s->nc2 + 3u) * s->D2;
    const uint32_t h1 = e1 * s->D2, h2 = (hi1 * 4u + 4u) * s->D2 + s->D2 + s->N;
    if (h1 > h) h = h1;
    if (h2 > h) h = h2;
    *hi = h;
}

WS_FN int32_t ws_mix(const ws_t *s, const int16_t *xl, const int16_t *xr, uint32_t i)
{
    return s->ch == 2u ? ((int32_t)xl[i] + (int32_t)xr[i]) >> 1 : (int32_t)xl[i];
}

WS_FN uint32_t ws_scale(const int16_t *v, uint32_t n)
{
    int32_t m = 0;
    for (uint32_t i = 0; i < n; i++) { int32_t a = v[i] < 0 ? -(int32_t)v[i] : v[i]; if (a > m) m = a; }
    uint32_t sc = 0;
    while ((m >> sc) > 1023) sc++;
    return sc;
}

static inline int32_t ws_clamp2047(int32_t v) { return v > 2047 ? 2047 : (v < -2047 ? -2047 : v); }

WS_FN uint32_t ws_nlz(uint32_t x)
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
WS_FN uint32_t ws_divlu(uint32_t u1, uint32_t u0, uint32_t v)
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
WS_FN int32_t ws_search(const int16_t *ref, const int16_t *cand, uint32_t ncand, uint32_t n)
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
WS_FN uint32_t ws_step(ws_t *s, const int16_t *xl, const int16_t *xr, uint32_t slab_lo, int16_t *outl, int16_t *outr)
{
    const uint32_t N = s->N, Hs = s->Hs;
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
    uint32_t tgt, p, c1, lo1, hi1, e0, e1;
    ws_geom(s, &tgt, &p, &c1, &lo1, &hi1, &e0, &e1);
    /* slab-relative views: index with absolute sample numbers */
    const int16_t *l = xl - slab_lo;
    const int16_t *r = xr ? xr - slab_lo : (const int16_t *)0;
    const uint32_t nc1 = s->nc1, nc2 = s->nc2, D2 = s->D2;

    /* the signal is decimated once, to the stage-2 rate (sum of D2 mixes, shifted); the stage-1 signal is the sum of four of those, shifted by 2 */
    int16_t R[128 + 3];                                    /* the reference region: nc2 + 3 stage-2 samples from the coarse grid at or below tgt */
    const uint32_t rbase = (tgt / s->D1) * 4u;
    for (uint32_t i = 0; i < nc2 + 3u; i++) {
        int32_t acc = 0;
        for (uint32_t k = 0; k < D2; k++) acc += ws_mix(s, l, r, (rbase + i) * D2 + k);
        R[i] = (int16_t)(acc >> s->shift);
    }
    int16_t ref1[32];
    for (uint32_t i = 0; i < nc1; i++) ref1[i] = (int16_t)(((int32_t)R[4u * i] + R[4u * i + 1u] + R[4u * i + 2u] + R[4u * i + 3u]) >> 2);
    const uint32_t sc1 = ws_scale(ref1, nc1);
    for (uint32_t i = 0; i < nc1; i++) ref1[i] = (int16_t)(ref1[i] >> sc1);

    int16_t C[272];                                        /* the candidate region: e1 - e0 <= 252 stage-2 samples */
    for (uint32_t i = 0; i < e1 - e0; i++) {
        int32_t acc = 0;
        for (uint32_t k = 0; k < D2; k++) acc += ws_mix(s, l, r, (e0 + i) * D2 + k);
        C[i] = (int16_t)(acc >> s->shift);
    }
    const uint32_t ncand1 = hi1 - lo1 + 1u;
    int16_t cand1[WS_R1 * 2u + 1u + 32u];
    for (uint32_t i = 0; i < ncand1 + nc1 - 1u; i++) {
        const uint32_t b = 4u * (lo1 + i) - e0;
        cand1[i] = (int16_t)ws_clamp2047((((int32_t)C[b] + C[b + 1u] + C[b + 2u] + C[b + 3u]) >> 2) >> sc1);
    }
    const int32_t idx1 = ws_search(ref1, cand1, ncand1, nc1);
    uint32_t w1;
    if (idx1 >= 0) w1 = lo1 + (uint32_t)idx1;
    else { w1 = c1; if (w1 < lo1) w1 = lo1; if (w1 > hi1) w1 = hi1; }

    /* stage 2: +-4 around stage 1's winner, window nc2 */
    const uint32_t c2 = w1 * 4u;
    const uint32_t lo2 = c2 > 4u ? c2 - 4u : 0u;
    const uint32_t ncand2 = c2 + 4u - lo2 + 1u;
    const uint32_t off2 = (tgt / D2) - rbase;
    int16_t ref2[128];
    for (uint32_t i = 0; i < nc2; i++) ref2[i] = R[off2 + i];
    const uint32_t sc2 = ws_scale(ref2, nc2);
    for (uint32_t i = 0; i < nc2; i++) ref2[i] = (int16_t)(ref2[i] >> sc2);
    int16_t cand2[9 + 128];
    for (uint32_t i = 0; i < ncand2 + nc2 - 1u; i++) cand2[i] = (int16_t)ws_clamp2047((int32_t)C[lo2 - e0 + i] >> sc2);
    const int32_t idx2 = ws_search(ref2, cand2, ncand2, nc2);
    const uint32_t w2 = idx2 >= 0 ? lo2 + (uint32_t)idx2 : c2;

    /* fine: +-D2 samples at the full rate, window WS_REF_LEN */
    const uint32_t c0 = w2 * D2;
    const uint32_t rlo = c0 > D2 ? c0 - D2 : 0u;
    const uint32_t nref = c0 + D2 - rlo + 1u;
    int16_t mixr[WS_REF_LEN + 2u * 8u + 1u];
    for (uint32_t j = 0; j < nref + WS_REF_LEN - 1u; j++) mixr[j] = (int16_t)ws_mix(s, l, r, rlo + j);
    int16_t rf[WS_REF_LEN];
    for (uint32_t i = 0; i < WS_REF_LEN; i++) rf[i] = (int16_t)ws_mix(s, l, r, tgt + i);
    const uint32_t s3 = ws_scale(rf, WS_REF_LEN);
    for (uint32_t i = 0; i < WS_REF_LEN; i++) rf[i] = (int16_t)(rf[i] >> s3);
    for (uint32_t j = 0; j < nref + WS_REF_LEN - 1u; j++) mixr[j] = (int16_t)ws_clamp2047((int32_t)mixr[j] >> s3);
    const int32_t idx3 = ws_search(rf, mixr, nref, WS_REF_LEN);
    const uint32_t c = idx3 >= 0 ? rlo + (uint32_t)idx3 : c0;

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

/* ===================================================================================================================================================================
 * Core v2 (Cymo C7 T1, B-556): the same algorithm and the same output sample for sample, restructured so it needs little on-chip memory and no contiguous window.
 *
 *   - The input is decimated ONCE, as it is fed in (ws2_feed): the stage-2-rate mono mix goes into a ring of WS2_RING entries (2 KB). Every decimated value v1 recomputed
 *     from the raw samples on each grain (about four times each) is now a ring read, and the values are identical, because they are defined on the same absolute grid.
 *   - The full-rate samples the fine stage and the grain need (about 1,300 per channel per grain) come through a caller-supplied reader, so they can live anywhere:
 *     the firmware reads them in blocks from a staging ring in PSRAM; the host test reads an array. The scratch is one 1 KB buffer reused by the stages.
 *   - On-chip: this struct (about 5 KB with the 2 KB overlap tail) plus 1 KB of stack. v1 needed a 5-17 KB window on top of its 2.1 KB of stack scratch.
 * Contract: feed samples in order (any chunk size, but step between feeds so the ring keeps the span the next step reads: at most 570 entries at 3.0x, so feed less than
 * about 450 entries (3,600 samples) between steps); a step is allowed when ws2_ready() says so; the reader must serve any range ws_need() names, from the start of the
 * stream (absolute sample numbers). */
#define WS2_RING 1024u

typedef void (*ws2_read_t)(void *ctx, uint32_t ch, uint32_t lo, uint32_t n, int16_t *dst);   /* copy n samples of channel ch starting at absolute sample lo */

typedef struct {
    ws_t     core;
    int16_t  ring[WS2_RING];      /* stage-2-rate mono mix, entry j holds the mix of samples j*D2 .. j*D2+D2-1 (shifted) */
    uint32_t fed;                 /* samples fed so far */
    int32_t  acc;                 /* running sum of the entry being built */
    uint32_t cnt;                 /* samples in it so far */
} ws2_t;

WS_FN void ws2_init(ws2_t *s, uint32_t fs, uint8_t ch, uint32_t speed_q8)
{
    ws_init(&s->core, fs, ch, speed_q8);
    s->fed = 0u; s->acc = 0; s->cnt = 0u;
    for (uint32_t i = 0; i < WS2_RING; i++) s->ring[i] = 0;
}

WS_FN void ws2_feed(ws2_t *s, const int16_t *l, const int16_t *r, uint32_t n)
{
    const uint32_t D2 = s->core.D2, sh = s->core.shift;
    const uint8_t st = s->core.ch;
    uint32_t i = 0;
    /* finish the entry in progress, one sample at a time */
    while (i < n && s->cnt != 0u) {
        s->acc += st == 2u ? ((int32_t)l[i] + (int32_t)r[i]) >> 1 : (int32_t)l[i];
        i++; s->fed++;
        if (++s->cnt == D2) { s->ring[((s->fed - 1u) / D2) & (WS2_RING - 1u)] = (int16_t)(s->acc >> sh); s->acc = 0; s->cnt = 0u; }
    }
    /* whole entries: D2 samples at a time with no per-sample bookkeeping (the hot path) */
    while (n - i >= D2) {
        int32_t a = 0;
        if (st == 2u) { for (uint32_t k = 0; k < D2; k++) a += ((int32_t)l[i + k] + (int32_t)r[i + k]) >> 1; }
        else          { const int16_t *q = l + i; for (uint32_t k = 0; k < D2; k++) a += q[k]; }
        s->ring[(s->fed / D2) & (WS2_RING - 1u)] = (int16_t)(a >> sh);
        s->fed += D2; i += D2;
    }
    /* the start of the next entry */
    while (i < n) {
        s->acc += st == 2u ? ((int32_t)l[i] + (int32_t)r[i]) >> 1 : (int32_t)l[i];
        i++; s->fed++; s->cnt++;
    }
}

/* The first decimated entry the next step reads (so the ring check below can tell whether it is still held). */
WS_FN uint32_t ws2_first_entry(const ws2_t *s)
{
    uint32_t tgt, p, c1, lo1, hi1, e0, e1;
    ws_geom(&s->core, &tgt, &p, &c1, &lo1, &hi1, &e0, &e1);
    const uint32_t rb = (tgt / s->core.D1) * 4u;
    return rb < e0 ? rb : e0;
}

/* 1 when the next ws2_step may run: enough samples have been fed and the ring still holds the oldest entry the step reads. */
WS_FN int ws2_ready(const ws2_t *s)
{
    uint32_t lo, hi;
    ws_need(&s->core, &lo, &hi);
    if (s->fed < hi) return 0;
    if (s->core.k == 0u) return 1;
    return (s->fed / s->core.D2) - ws2_first_entry(s) <= WS2_RING;
}

WS_FN uint32_t ws2_step(ws2_t *s, ws2_read_t rd, void *ctx, int16_t *outl, int16_t *outr)
{
    ws_t *c = &s->core;
    const uint32_t N = c->N, Hs = c->Hs, D2 = c->D2, nc1 = c->nc1, nc2 = c->nc2;
    int16_t *outs[2] = { outl, outr };
    int16_t buf[512];
    if (c->k == 0u) {
        for (uint32_t ch = 0; ch < c->ch; ch++) {
            rd(ctx, ch, 0u, Hs, outs[ch]);
            rd(ctx, ch, Hs, Hs, c->pend[ch]);
        }
        c->prev = 0u;
        c->k = 1u;
        return Hs;
    }
    uint32_t tgt, p, c1, lo1, hi1, e0, e1;
    ws_geom(c, &tgt, &p, &c1, &lo1, &hi1, &e0, &e1);
    const int16_t *R = s->ring;
#define WS2_E(j) ((int32_t)R[(j) & (WS2_RING - 1u)])
    /* stage 1 */
    const uint32_t rbase = (tgt / c->D1) * 4u;
    int16_t *ref1 = buf;                 /* [0, 32) */
    for (uint32_t i = 0; i < nc1; i++) ref1[i] = (int16_t)((WS2_E(rbase + 4u * i) + WS2_E(rbase + 4u * i + 1u) + WS2_E(rbase + 4u * i + 2u) + WS2_E(rbase + 4u * i + 3u)) >> 2);
    const uint32_t sc1 = ws_scale(ref1, nc1);
    for (uint32_t i = 0; i < nc1; i++) ref1[i] = (int16_t)(ref1[i] >> sc1);
    const uint32_t ncand1 = hi1 - lo1 + 1u;
    int16_t *cand1 = buf + 32;           /* [32, 92) */
    for (uint32_t i = 0; i < ncand1 + nc1 - 1u; i++) {
        const uint32_t b = 4u * (lo1 + i);
        cand1[i] = (int16_t)ws_clamp2047(((WS2_E(b) + WS2_E(b + 1u) + WS2_E(b + 2u) + WS2_E(b + 3u)) >> 2) >> sc1);
    }
    const int32_t idx1 = ws_search(ref1, cand1, ncand1, nc1);
    uint32_t w1;
    if (idx1 >= 0) w1 = lo1 + (uint32_t)idx1;
    else { w1 = c1; if (w1 < lo1) w1 = lo1; if (w1 > hi1) w1 = hi1; }
    /* stage 2 */
    const uint32_t c2 = w1 * 4u;
    const uint32_t lo2 = c2 > 4u ? c2 - 4u : 0u;
    const uint32_t ncand2 = c2 + 4u - lo2 + 1u;
    const uint32_t off2 = (tgt / D2);
    int16_t *ref2 = buf;                 /* [0, 128) */
    for (uint32_t i = 0; i < nc2; i++) ref2[i] = R[(off2 + i) & (WS2_RING - 1u)];
    const uint32_t sc2 = ws_scale(ref2, nc2);
    for (uint32_t i = 0; i < nc2; i++) ref2[i] = (int16_t)(ref2[i] >> sc2);
    int16_t *cand2 = buf + 128;          /* [128, 264) */
    for (uint32_t i = 0; i < ncand2 + nc2 - 1u; i++) cand2[i] = (int16_t)ws_clamp2047(WS2_E(lo2 + i) >> sc2);
    const int32_t idx2 = ws_search(ref2, cand2, ncand2, nc2);
    const uint32_t w2 = idx2 >= 0 ? lo2 + (uint32_t)idx2 : c2;
#undef WS2_E
    /* fine: the full-rate window comes through the reader (mono mix of both channels when stereo) */
    const uint32_t c0 = w2 * D2;
    const uint32_t rlo = c0 > D2 ? c0 - D2 : 0u;
    const uint32_t nref = c0 + D2 - rlo + 1u;
    const uint32_t nm = nref + WS_REF_LEN - 1u;
    int16_t *mixr = buf;                 /* [0, nm) <= 144 */
    int16_t *rf = buf + 160;             /* [160, 288) */
    int16_t *tmp = buf + 288;            /* [288, 432): the right channel while mixing */
    rd(ctx, 0u, rlo, nm, mixr);
    rd(ctx, 0u, tgt, WS_REF_LEN, rf);
    if (c->ch == 2u) {
        rd(ctx, 1u, rlo, nm, tmp);
        for (uint32_t j = 0; j < nm; j++) mixr[j] = (int16_t)(((int32_t)mixr[j] + tmp[j]) >> 1);
        rd(ctx, 1u, tgt, WS_REF_LEN, tmp);
        for (uint32_t i = 0; i < WS_REF_LEN; i++) rf[i] = (int16_t)(((int32_t)rf[i] + tmp[i]) >> 1);
    }
    const uint32_t s3 = ws_scale(rf, WS_REF_LEN);
    for (uint32_t i = 0; i < WS_REF_LEN; i++) rf[i] = (int16_t)(rf[i] >> s3);
    for (uint32_t j = 0; j < nm; j++) mixr[j] = (int16_t)ws_clamp2047((int32_t)mixr[j] >> s3);
    const int32_t idx3 = ws_search(rf, mixr, nref, WS_REF_LEN);
    const uint32_t cg = idx3 >= 0 ? rlo + (uint32_t)idx3 : c0;
    /* overlap-add the chosen grain: the first half is read into the scratch and mixed with the tail, the second half becomes the new tail directly */
    const uint32_t stp = (N == 1024u) ? 1u : 2u;
    for (uint32_t ch = 0; ch < c->ch; ch++) {
        rd(ctx, ch, cg, Hs, buf);
        for (uint32_t i = 0; i < Hs; i++) {
            const int32_t w = ws_hann_q15[i * stp];
            outs[ch][i] = (int16_t)(((int32_t)c->pend[ch][i] * (WS_Q15 - w) + (int32_t)buf[i] * w + 16384) >> 15);
        }
        rd(ctx, ch, cg + Hs, Hs, c->pend[ch]);
    }
    c->prev = cg;
    c->p_q8 += (uint64_t)Hs * c->speed_q8;
    c->k++;
    return Hs;
}
#endif
