/* fw2/cores/flac2_core.h -- the Player 2 FLAC decoder core (spec: docs/features/FLAC_DECODER_V2_REVIEW.md section 5), written from RFC 9639, NOT from fw/flac.c. Pure (no I/O, no malloc, no globals) and
 * host-tested (sim/test_p2_flac2.py) against independent encoders/oracles.
 *
 * Shape: a RESUMABLE decoder. The caller owns the input. Every call is given a window of bytes and returns how far it consumed; the decoder keeps only a small bit reservoir between calls, so the
 * input may arrive in pieces of any size. Every unit of work is ATOMIC: it either completes or returns F2_NEED having consumed NOTHING (the reservoir and window pointer are restored), so the caller
 * can present more bytes and call again. Output is identical for any slicing of the work and any chunking of the input (the property sim/test_p2_flac2.py proves with random sequences).
 *
 * This file grows piece by piece (S8-1): 1 bit reader + Rice residuals; 2 predictors; 3 header/CRC; 4 frame machine. */
#ifndef FLAC2_CORE_H
#define FLAC2_CORE_H
#include <stdint.h>
#include <stddef.h>
#ifndef BUG
#define BUG 0
#endif

enum { F2_OK = 0, F2_NEED = 1, F2_ERR = 2 };

/* ---- bit reader: a left-aligned 32-bit window `w` holding `nb` valid bits (the top nb bits), refilled bytewise from the caller's window [p, end). ---- */
typedef struct { const uint8_t *p, *end; uint32_t w; uint32_t nb; } f2_br_t;

static inline uint32_t f2_shl(uint32_t v, uint32_t n) { return n >= 32u ? 0u : v << n; }
static inline void f2_refill(f2_br_t *b) { while (b->nb <= 24u && b->p < b->end) { b->w |= (uint32_t)(*b->p++) << (24u - b->nb); b->nb += 8u; } }
static inline uint32_t f2_clz(uint32_t x)            /* x != 0 */
{
    uint32_t n = 0;
    if (!(x & 0xFFFF0000u)) { n += 16; x <<= 16; }
    if (!(x & 0xFF000000u)) { n += 8;  x <<= 8; }
    if (!(x & 0xF0000000u)) { n += 4;  x <<= 4; }
    if (!(x & 0xC0000000u)) { n += 2;  x <<= 2; }
    if (!(x & 0x80000000u)) { n += 1; }
    return n;
}
/* Drops n (<= nb) bits. */
static inline void f2_drop(f2_br_t *b, uint32_t n) { b->w = f2_shl(b->w, n); b->nb -= n; }
/* Reads n bits (0..32). Returns 0 and leaves the reader untouched when fewer than n bits remain in reservoir + window. */
static inline int f2_get(f2_br_t *b, uint32_t n, uint32_t *v)
{
    if (n == 0) { *v = 0; return 1; }
    if (n > 16u) {                                        /* two halves: the reservoir only guarantees 25 bits after a refill */
        f2_br_t s = *b; uint32_t hi, lo;
        if (!f2_get(b, n - 16u, &hi) || !f2_get(b, 16u, &lo)) { *b = s; return 0; }
#if BUG == 6
        *v = (lo << 16) | hi; return 1;                       /* halves swapped */
#else
        *v = (hi << 16) | lo; return 1;
#endif
    }
    f2_refill(b);
    if (b->nb < n) return 0;
    *v = b->w >> (32u - n); f2_drop(b, n);
    return 1;
}
static inline uint32_t f2_sext(uint32_t v, uint32_t n) { return n == 0 ? 0u : (n >= 32u ? v : ((v ^ (1u << (n - 1u))) - (1u << (n - 1u)))); }

/* ---- Rice-coded residual (RFC 9639 section 9.2.7): partitioned, methods 0 and 1, escape-coded partitions ---- */
typedef struct {
    uint32_t bs, order;               /* block size and predictor order (the first partition holds `order` fewer residuals) */
    uint32_t parts, per, part;        /* partition count, samples per partition, index of the partition in progress (parts when finished) */
    uint32_t left;                    /* residuals left in the partition in progress */
    uint8_t  pbits, esc;              /* parameter width (4 or 5) and the escape value (15 or 31) */
    uint8_t  k, raw, is_esc;          /* Rice parameter, raw width of an escape partition, escape flag */
} f2_res_t;

#define F2_QCAP 4096u                 /* longest unary run accepted: far beyond anything an encoder emits (it bounds the work a corrupt stream can demand) */

/* Reads method (2 bits) and partition order (4 bits). */
static inline int f2_res_begin(f2_br_t *b, f2_res_t *r, uint32_t bs, uint32_t order)
{
    f2_br_t s = *b; uint32_t m, po;
    if (!f2_get(b, 2, &m) || !f2_get(b, 4, &po)) { *b = s; return F2_NEED; }
    if (m > 1u) return F2_ERR;                           /* methods 2 and 3 are reserved */
    r->pbits = m ? 5 : 4; r->esc = m ? 31 : 15;
    r->parts = 1u << po;
    if (bs % r->parts) return F2_ERR;                    /* the block size must divide by 2^order */
    r->per = bs / r->parts; r->bs = bs; r->order = order;
    if (r->per < order) return F2_ERR;                   /* the first partition would hold a negative count */
    r->part = 0; r->left = 0; r->is_esc = 0; r->k = 0; r->raw = 0;
    return F2_OK;
}
static inline int f2_res_done(const f2_res_t *r) { return r->part >= r->parts && r->left == 0; }

/* One Rice value, atomic: F2_OK (value in *out), F2_NEED (logical position unchanged) or F2_ERR. A value must fit 32 bits before the zigzag fold: q < 2^(32-k) for k >= 1. */
#if BUG == 4
static inline int32_t f2_unzig(uint32_t u) { return (u & 1u) ? -(int32_t)(u >> 1) : (int32_t)(u >> 1); }      /* off by one for odd values */
#else
static inline int32_t f2_unzig(uint32_t u) { return (int32_t)((u >> 1) ^ (0u - (u & 1u))); }
#endif
static inline int f2_rice_one(f2_br_t *b, uint32_t k, int32_t *out)
{
    f2_refill(b);
    if (b->w != 0u) {                                         /* fast path: the unary run and the remainder are both inside the reservoir */
        uint32_t q = f2_clz(b->w);
        if (q + 1u + k <= b->nb) {
#if BUG == 2
            uint32_t rem = k ? f2_shl(b->w, q) >> (32u - k) : 0u;                  /* forgets the stop bit */
#else
            uint32_t rem = k ? f2_shl(b->w, q + 1u) >> (32u - k) : 0u;
#endif
            f2_drop(b, q + 1u + k);
            *out = f2_unzig((q << k) | rem);                  /* q <= 31 and q + 1 + k <= 32 here, so q << k cannot pass 32 bits */
            return F2_OK;
        }
    }
    {   /* slow path: scan the unary run bit by bit, restoring the reader if the window runs dry */
        f2_br_t s = *b; uint32_t q = 0, bit, rem = 0;
        for (;;) {
#if BUG == 7
            if (!f2_get(b, 1, &bit)) { return F2_NEED; }                       /* the reader is not restored */
#else
            if (!f2_get(b, 1, &bit)) { *b = s; return F2_NEED; }
#endif
            if (bit) break;
            if (++q > F2_QCAP) { *b = s; return F2_ERR; }
        }
        if (k && (q >> (32u - k))) { *b = s; return F2_ERR; }
        if (k && !f2_get(b, k, &rem)) { *b = s; return F2_NEED; }
        *out = f2_unzig((q << k) | rem);
        return F2_OK;
    }
}

/* Decodes up to `n` residuals into out[0..], crossing partition boundaries; *done = how many were produced. Returns F2_OK when n were produced or the block is finished, F2_NEED when the
 * window ran dry (the residuals already produced stay produced; the failed one consumed nothing) or F2_ERR. */
static inline int f2_res_run(f2_br_t *b, f2_res_t *r, int32_t *out, uint32_t n, uint32_t *done)
{
    uint32_t d = 0; int rc = F2_OK;
    while (d < n) {
        if (r->left == 0u) {
            if (r->part >= r->parts) break;                                     /* block finished */
            f2_br_t s = *b; uint32_t par, raw = 0;
            if (!f2_get(b, r->pbits, &par)) { *b = s; rc = F2_NEED; break; }
            r->is_esc = (par == r->esc);
#if BUG == 3
            if (r->is_esc && !f2_get(b, 5, &raw)) { rc = F2_NEED; break; }     /* partial header consumed */
#else
            if (r->is_esc && !f2_get(b, 5, &raw)) { *b = s; rc = F2_NEED; break; }
#endif
            r->k = (uint8_t)par; r->raw = (uint8_t)raw;
#if BUG == 5
            r->left = r->per;                                                   /* ignores the predictor order */
#else
            r->left = r->per - (r->part == 0u ? r->order : 0u);
#endif
            r->part++;
            continue;                                                           /* (an empty partition loops straight to the next header) */
        }
        if (r->is_esc) {
            uint32_t v;
#if BUG == 1
            if (!f2_get(b, r->raw ? r->raw : 1u, &v)) { rc = F2_NEED; break; }   /* raw width 0 read as 1 bit */
            out[d++] = (int32_t)f2_sext(v, r->raw ? r->raw : 1u); r->left--;
#else
            if (!f2_get(b, r->raw, &v)) { rc = F2_NEED; break; }
            out[d++] = (int32_t)f2_sext(v, r->raw); r->left--;
#endif
        } else {
            int32_t v; int e = f2_rice_one(b, r->k, &v);
            if (e != F2_OK) { rc = e; break; }
            out[d++] = v; r->left--;
        }
    }
    *done = d;
    return rc;
}

#endif
