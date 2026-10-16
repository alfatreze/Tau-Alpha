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


/* ---- predictors (RFC 9639 sections 9.2.5 and 9.2.6): in place on x[], residuals already stored at x[order..], warm-up samples at x[0..order-1] ----
 * All sums use unsigned (wrapping) arithmetic, so a corrupt stream can never trigger signed-overflow undefined behaviour; a VALID stream never wraps. Three tiers for LPC:
 *   32-bit  exact when the worst case fits: with |x| <= 2^(b-1) (b = bits of the samples being predicted, +1 for a side channel, less the wasted bits) and S = sum |c_j|, the sum is below
 *           2^31 when S * 2^(b-1) < 2^31, i.e. S < 2^(32-b). Almost every 16-bit predictor passes (measured on real files, review doc 11.5); 24-bit mostly does not.
 *   64-bit  always exact (|c| < 2^15, |x| < 2^32, order <= 32 keep the sum below 2^52).
 *   the hardware unit (not in this file) for the cases the 32-bit tier cannot take. */
typedef struct { uint8_t type, order, shift, tier; int32_t coef[32]; } f2_pred_t;      /* type 1 FIXED, 2 LPC; tier chosen by f2_pred_lpc_set */
enum { F2_TIER_NONE = 0, F2_TIER_32 = 1, F2_TIER_64 = 2 };

static inline int f2_lpc_fits32(const int32_t *coef, uint32_t order, uint32_t b)
{
    uint32_t S = 0;
    if (b >= 32u) return 0;
    for (uint32_t j = 0; j < order; j++) { uint32_t a = coef[j] < 0 ? 0u - (uint32_t)coef[j] : (uint32_t)coef[j]; S += a; }
#if BUG == 8
    return S <= (1u << (32u - b));                                  /* off by one: the extreme operands can reach +2^31 */
#else
    return S < (1u << (32u - b));
#endif
}
static inline void f2_pred_fixed_set(f2_pred_t *p, uint32_t order) { p->type = 1; p->order = (uint8_t)order; p->shift = 0; p->tier = F2_TIER_32; }
/* force: 0 = choose by the bound, 1 = 64-bit, 2 = 32-bit (tests only: forcing 32 on a predictor that does not fit must be wrong, which is what the bound prevents) */
static inline void f2_pred_lpc_set(f2_pred_t *p, uint32_t order, uint32_t shift, const int32_t *coef, uint32_t b, int force)
{
    p->type = 2; p->order = (uint8_t)order; p->shift = (uint8_t)shift;
    for (uint32_t j = 0; j < order; j++) p->coef[j] = coef[j];
    p->tier = force == 1 ? F2_TIER_64 : force == 2 ? F2_TIER_32 : f2_lpc_fits32(coef, order, b) ? F2_TIER_32 : F2_TIER_64;
}
/* Predicts samples from..to-1 (from >= order). */
static inline void f2_pred_run(const f2_pred_t *p, int32_t *x, uint32_t from, uint32_t to)
{
    uint32_t order = p->order;
    if (p->type == 1) {                                             /* FIXED: the polynomial predictors, written out */
        for (uint32_t i = from; i < to; i++) {
            uint32_t a = order > 0 ? (uint32_t)x[i - 1] : 0u, b = order > 1 ? (uint32_t)x[i - 2] : 0u, c = order > 2 ? (uint32_t)x[i - 3] : 0u, d = order > 3 ? (uint32_t)x[i - 4] : 0u, pr;
            switch (order) {
            case 0: pr = 0u; break;
            case 1: pr = a; break;
            case 2: pr = 2u * a - b; break;
            case 3: pr = 3u * a - 3u * b + c; break;
            default: pr = 4u * a - 6u * b + 4u * c - d; break;
            }
            x[i] = (int32_t)((uint32_t)x[i] + pr);
        }
        return;
    }
    if (p->tier == F2_TIER_32) {
        for (uint32_t i = from; i < to; i++) {
            uint32_t acc = 0;
            for (uint32_t j = 0; j < order; j++) acc += (uint32_t)p->coef[j] * (uint32_t)x[i - 1u - j];
            x[i] = (int32_t)((uint32_t)x[i] + (uint32_t)((int32_t)acc >> p->shift));
        }
    } else {
        for (uint32_t i = from; i < to; i++) {
            int64_t acc = 0;
            for (uint32_t j = 0; j < order; j++) acc += (int64_t)p->coef[j] * (int64_t)x[i - 1u - j];
#if BUG == 9
            x[i] = (int32_t)((uint32_t)x[i] + (uint32_t)(int32_t)(acc >> (p->shift + 1u)));      /* shift off by one */
#else
            x[i] = (int32_t)((uint32_t)x[i] + (uint32_t)(int32_t)(acc >> p->shift));
#endif
        }
    }
}


/* ---- CRCs (RFC 9639 sections 9.1.8 and 9.3.2) ---- */
/* CRC-8, polynomial 0x07, initial value 0: the frame header. */
static inline uint8_t f2_crc8(const uint8_t *p, uint32_t n)
{
    uint8_t c = 0;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (uint8_t)((c & 0x80u) ? ((c << 1) ^ 0x07u) : (c << 1));
    }
    return c;
}
/* CRC-16, polynomial 0x8005, initial value 0, not reflected: the whole frame (sync code to the last padding bit). Two 16-entry steps per byte (32 bytes of table instead of 512). */
#if BUG == 10
static const uint16_t f2_crc16_nib[16] = { 0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011, 0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0023 };   /* one wrong entry */
#else
static const uint16_t f2_crc16_nib[16] = { 0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011, 0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022 };
#endif
static inline uint16_t f2_crc16(uint16_t c, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        c = (uint16_t)(c ^ ((uint16_t)p[i] << 8));
        c = (uint16_t)((c << 4) ^ f2_crc16_nib[c >> 12]);
        c = (uint16_t)((c << 4) ^ f2_crc16_nib[c >> 12]);
    }
    return c;
}

/* ---- STREAMINFO (RFC 9639 section 8.2): the 34-byte body ---- */
typedef struct { uint32_t min_bs, max_bs, rate; uint8_t channels, bps; uint64_t total; } f2_info_t;
static inline int f2_streaminfo(const uint8_t *p, f2_info_t *si)
{
    si->min_bs = ((uint32_t)p[0] << 8) | p[1];
    si->max_bs = ((uint32_t)p[2] << 8) | p[3];
    si->rate = ((uint32_t)p[10] << 12) | ((uint32_t)p[11] << 4) | (p[12] >> 4);
    si->channels = (uint8_t)(((p[12] >> 1) & 7u) + 1u);
    si->bps = (uint8_t)((((p[12] & 1u) << 4) | (p[13] >> 4)) + 1u);
    si->total = ((uint64_t)(p[13] & 15u) << 32) | ((uint64_t)p[14] << 24) | ((uint64_t)p[15] << 16) | ((uint64_t)p[16] << 8) | p[17];
    if (si->min_bs < 16u || si->max_bs < si->min_bs || si->rate == 0u) return F2_ERR;     /* block sizes under 16 and a zero rate are invalid */
    return F2_OK;
}

/* ---- frame header (RFC 9639 section 9.1) ---- */
enum { F2E_NONE = 0, F2E_SYNC, F2E_RESERVED, F2E_CRC8, F2E_MISMATCH, F2E_BLOCKSIZE, F2E_NUMBER };
enum { F2_CH_INDEP = 0, F2_CH_LEFT_SIDE = 8, F2_CH_SIDE_RIGHT = 9, F2_CH_MID_SIDE = 10 };
typedef struct {
    uint32_t bs, rate, hdr_len;       /* block size, sample rate, header length in bytes (CRC-8 included) */
    uint64_t number;                  /* frame number (fixed block size) or the first sample number (variable block size) */
    uint8_t  variable, assign, chans, bps, err;
} f2_fh_t;

/* Decodes the UTF-8-style coded number at p (up to 7 bytes, up to 36 bits). Returns the byte count, 0 for need-more, -1 for invalid. */
static inline int f2_coded_number(const uint8_t *p, uint32_t avail, uint64_t *v)
{
    if (avail == 0) return 0;
    uint8_t b0 = p[0]; uint32_t len; uint64_t x;
    if (b0 < 0x80u) { *v = b0; return 1; }
    if ((b0 & 0xE0u) == 0xC0u) { len = 2; x = b0 & 0x1Fu; }
    else if ((b0 & 0xF0u) == 0xE0u) { len = 3; x = b0 & 0x0Fu; }
    else if ((b0 & 0xF8u) == 0xF0u) { len = 4; x = b0 & 0x07u; }
    else if ((b0 & 0xFCu) == 0xF8u) { len = 5; x = b0 & 0x03u; }
    else if ((b0 & 0xFEu) == 0xFCu) { len = 6; x = b0 & 0x01u; }
    else if (b0 == 0xFEu) { len = 7; x = 0; }
    else return -1;                                                 /* 10xxxxxx (a continuation byte) or 0xFF */
#if BUG == 11
    if (len > 6) return -1;                                         /* the 7-byte form (36 bits) refused */
#endif
    for (uint32_t i = 1; i < len; i++) {
        if (i >= avail) return 0;
        if ((p[i] & 0xC0u) != 0x80u) return -1;
        x = (x << 6) | (p[i] & 0x3Fu);
    }
    *v = x; return (int)len;
}

/* Parses a frame header at p[0..avail) against STREAMINFO. Returns F2_OK (h filled, h->hdr_len bytes), F2_NEED (a valid prefix: more bytes wanted; nothing is wrong yet) or F2_ERR with h->err.
 * Anything detectable from the bytes present is reported at once, so a prefix of a VALID header never returns ERR. */
static inline int f2_frame_header(const uint8_t *p, uint32_t avail, const f2_info_t *si, f2_fh_t *h)
{
    static const uint16_t bsfix[16] = { 0, 192, 576, 1152, 2304, 4608, 0, 0, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
    static const uint32_t ratefix[16] = { 0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, 0, 0, 0, 0 };
    static const uint8_t bpsfix[8] = { 0, 8, 12, 0, 16, 20, 24, 32 };
    h->err = F2E_NONE;
    if (avail >= 1 && p[0] != 0xFFu) { h->err = F2E_SYNC; return F2_ERR; }
    if (avail >= 2 && (p[1] & 0xFEu) != 0xF8u) { h->err = F2E_SYNC; return F2_ERR; }
    if (avail < 4) return F2_NEED;
    uint32_t bsc = p[2] >> 4, rc = p[2] & 15u, ac = p[3] >> 4, sc = (p[3] >> 1) & 7u;
    if (p[3] & 1u) { h->err = F2E_RESERVED; return F2_ERR; }
    if (bsc == 0 || rc == 15u || ac > 10u || sc == 3u) { h->err = F2E_RESERVED; return F2_ERR; }
    h->variable = p[1] & 1u; h->assign = (uint8_t)ac;
    h->chans = ac < 8u ? (uint8_t)(ac + 1u) : 2u;
    uint32_t n = 4; int k = f2_coded_number(p + n, avail - n, &h->number);
    if (k < 0) { h->err = F2E_NUMBER; return F2_ERR; }
    if (k == 0) return F2_NEED;
    if (h->variable ? (h->number >> 36) != 0 : (h->number >> 31) != 0) { h->err = F2E_NUMBER; return F2_ERR; }
    n += (uint32_t)k;
    if (bsc == 6u) { if (avail < n + 1u) return F2_NEED; h->bs = (uint32_t)p[n] + 1u; n += 1u; }
    else if (bsc == 7u) { if (avail < n + 2u) return F2_NEED; h->bs = (((uint32_t)p[n] << 8) | p[n + 1u]) + 1u; n += 2u; }
    else h->bs = bsfix[bsc];
    if (rc == 12u) { if (avail < n + 1u) return F2_NEED; h->rate = (uint32_t)p[n] * 1000u; n += 1u; }
    else if (rc == 13u) { if (avail < n + 2u) return F2_NEED; h->rate = ((uint32_t)p[n] << 8) | p[n + 1u]; n += 2u; }
    else if (rc == 14u) { if (avail < n + 2u) return F2_NEED; h->rate = (((uint32_t)p[n] << 8) | p[n + 1u]) * 10u; n += 2u; }
    else h->rate = rc ? ratefix[rc] : si->rate;
    if (avail < n + 1u) return F2_NEED;
    h->hdr_len = n + 1u;
    if (f2_crc8(p, n) != p[n]) { h->err = F2E_CRC8; return F2_ERR; }
    h->bps = sc ? bpsfix[sc] : si->bps;
    if (h->chans != si->channels || h->rate != si->rate || h->bps != si->bps || h->rate == 0u) { h->err = F2E_MISMATCH; return F2_ERR; }
    if (h->bs > si->max_bs || h->bs == 0u) { h->err = F2E_BLOCKSIZE; return F2_ERR; }
    return F2_OK;
}

#endif
