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
static inline __attribute__((always_inline)) uint32_t f2_clz(uint32_t x)            /* x != 0; the quotient is 0, 1 or 2 for most values with a well-chosen k, so those are tested first */
{
    if (x & 0x80000000u) return 0;
    if (x & 0x40000000u) return 1;
    if (x & 0x20000000u) return 2;
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

/* The hot loop: up to m values of parameter k from a left-aligned window kept in locals (refilled 16 bits at a time). It only ever consumes COMPLETE values that are fully inside the window, so it
 * needs no rollback; it stops (returning how many it did) at the first value that is not ordinary -- a unary run past the window, a value wider than the window, no 2 bytes left to refill --
 * and the caller hands that one value to f2_rice_one. */
static inline uint32_t f2_rice_fast(f2_br_t *b, uint32_t k, int32_t *out, uint32_t m)
{
    uint32_t w = b->w, nb = b->nb, i = 0; const uint8_t *p = b->p, *end = b->end;
    for (; i < m; i++) {
        if (nb <= 16u && p + 2 <= end) { w |= (((uint32_t)p[0] << 8) | p[1]) << (16u - nb); p += 2; nb += 16u; }
        if (w == 0u) break;
        uint32_t q = f2_clz(w), used = q + 1u + k;
        if (used > nb) break;
        uint32_t t = (w << q) << 1;                                   /* the zeros and the stop bit dropped */
#if BUG == 2
        uint32_t u = (q << k) | ((w << q) >> 1 >> (31u - k));         /* forgets the stop bit */
#else
        uint32_t u = (q << k) | ((t >> 1) >> (31u - k));              /* k = 0 gives 0 */
#endif
        w = t << k; nb -= used;                                       /* used <= nb <= 32 keeps both shifts below 32 */
        out[i] = f2_unzig(u);
    }
    b->w = w; b->nb = nb; b->p = p;
    return i;
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
            uint32_t m = r->left < n - d ? r->left : n - d, got = f2_rice_fast(b, r->k, out + d, m);
            d += got; r->left -= got;
            if (got < m) {                                                      /* one value off the fast path */
                int32_t v; int e = f2_rice_one(b, r->k, &v);
                if (e != F2_OK) { rc = e; break; }
                out[d++] = v; r->left--;
            }
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
/* CRC-16, polynomial 0x8005, initial value 0, not reflected: the whole frame (sync code to the last padding bit). A 256-entry table (512 bytes of rodata): about 7 cycles a byte on rv32im, where two
 * 16-entry nibble steps cost 24 (measured, S8-1c). */
static const uint16_t f2_crc16_tab[256] = {
    0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
    0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
    0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
    0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
    0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
    0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
    0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
    0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
    0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
    0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
    0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
    0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
    0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
    0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
    0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
    0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
    0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
    0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
    0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
    0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
    0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
    0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
    0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
    0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
    0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
    0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
    0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
    0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
    0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
    0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
    0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
    0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202
};
static inline uint16_t f2_crc16(uint16_t c, const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
#if BUG == 10
        c = (uint16_t)((c << 8) ^ f2_crc16_tab[((c >> 8) ^ p[i] ^ (i == 3)) & 0xFFu]);          /* a corrupted lookup */
#else
        c = (uint16_t)((c << 8) ^ f2_crc16_tab[((c >> 8) ^ p[i]) & 0xFFu]);
#endif
    }
    return c;
}

/* ---- STREAMINFO (RFC 9639 section 8.2): the 34-byte body ---- */
typedef struct { uint32_t min_bs, max_bs, rate; uint8_t channels, bps; uint32_t total; } f2_info_t;      /* total: samples per channel; 0 = unknown, and anything past 2^32 (this player counts positions in 32 bits) */
static inline int f2_streaminfo(const uint8_t *p, f2_info_t *si)
{
    si->min_bs = ((uint32_t)p[0] << 8) | p[1];
    si->max_bs = ((uint32_t)p[2] << 8) | p[3];
    si->rate = ((uint32_t)p[10] << 12) | ((uint32_t)p[11] << 4) | (p[12] >> 4);
    si->channels = (uint8_t)(((p[12] >> 1) & 7u) + 1u);
    si->bps = (uint8_t)((((p[12] & 1u) << 4) | (p[13] >> 4)) + 1u);
    si->total = (p[13] & 15u) ? 0u : (((uint32_t)p[14] << 24) | ((uint32_t)p[15] << 16) | ((uint32_t)p[16] << 8) | p[17]);
    if (si->min_bs < 16u || si->max_bs < si->min_bs || si->rate == 0u) return F2_ERR;     /* block sizes under 16 and a zero rate are invalid */
    return F2_OK;
}

/* ---- frame header (RFC 9639 section 9.1) ---- */
enum { F2E_NONE = 0, F2E_SYNC, F2E_RESERVED, F2E_CRC8, F2E_MISMATCH, F2E_BLOCKSIZE, F2E_NUMBER, F2E_CRC16, F2E_SUBFRAME, F2E_UNSUPPORTED, F2E_RESIDUAL };
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


/* ---- one prediction at index i (the streaming channel): history read through `ws` wasted bits (the buffer holds shifted values) ---- */
static inline uint32_t f2_pred_at(const f2_pred_t *p, const int32_t *x, uint32_t i, uint32_t ws)
{
    uint32_t order = p->order;
    if (p->type == 1) {
        uint32_t a = order > 0 ? (uint32_t)(x[i - 1] >> ws) : 0u, b = order > 1 ? (uint32_t)(x[i - 2] >> ws) : 0u, c = order > 2 ? (uint32_t)(x[i - 3] >> ws) : 0u, d = order > 3 ? (uint32_t)(x[i - 4] >> ws) : 0u;
        switch (order) { case 0: return 0u; case 1: return a; case 2: return 2u * a - b; case 3: return 3u * a - 3u * b + c; default: return 4u * a - 6u * b + 4u * c - d; }
    }
    if (p->tier == F2_TIER_32) {
        const int32_t *c = p->coef, *xp = x + i - 1; uint32_t acc = 0, j = 0;
        if (ws == 0u) {
            for (; j + 4u <= order; j += 4u)
                acc += (uint32_t)c[j] * (uint32_t)xp[-(int)j] + (uint32_t)c[j + 1u] * (uint32_t)xp[-(int)j - 1] + (uint32_t)c[j + 2u] * (uint32_t)xp[-(int)j - 2] + (uint32_t)c[j + 3u] * (uint32_t)xp[-(int)j - 3];
            for (; j < order; j++) acc += (uint32_t)c[j] * (uint32_t)xp[-(int)j];
        } else for (; j < order; j++) acc += (uint32_t)c[j] * (uint32_t)(xp[-(int)j] >> ws);
        return (uint32_t)((int32_t)acc >> p->shift);
    }
    int64_t acc = 0;
    for (uint32_t j = 0; j < order; j++) acc += (int64_t)p->coef[j] * (int64_t)(x[i - 1u - j] >> ws);
#if BUG == 9
    return (uint32_t)(int32_t)(acc >> (p->shift + 1u));                         /* shift off by one */
#else
    return (uint32_t)(int32_t)(acc >> p->shift);
#endif
}
/* Predicts samples from..to-1 in place (from >= order): residuals in x[], the prediction added. One implementation serves both channels (the 192 KB link cannot afford two copies of the tap loops). */
static inline void f2_pred_run(const f2_pred_t *p, int32_t *x, uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to; i++) x[i] = (int32_t)((uint32_t)x[i] + f2_pred_at(p, x, i, 0u));
}

/* ---- the frame decoder: a resumable state machine (spec 5.1 to 5.6) ---- */
enum { F2_PH_HDR = 0, F2_PH_SUB, F2_PH_SETUP, F2_PH_BODY, F2_PH_EMIT, F2_PH_TRAILER };
#define F2_RS 64u                                       /* residuals handled per inner slice */
#define F2_FLAG_CRC16 1u
/* The hardware LPC unit (tau_flac_lpc.sv), seen through two calls so the core stays pure. It takes over the prediction when the 32-bit software tier cannot be exact (24-bit audio, long high-precision predictors).
 * begin: order, shift, coefficients, warm-up samples (oldest first); returns 0 if the unit is not usable. sample: the residual in, the reconstructed sample out; *ok = 0 on a fault, after which the unit is
 * never used again this session and the sample is finished in software from the same history. */
typedef struct { int (*begin)(uint32_t order, int32_t shift, const int32_t *coef, const int32_t *warm); int32_t (*sample)(int32_t residual, int *ok); } f2_hw_t;
typedef struct {
    f2_info_t si; f2_fh_t fh;
    const f2_hw_t *hw; uint8_t hw_on; uint32_t hw_samples, hw_fails;          /* hw: the unit (0 = software only); hw_on: it is running the subframe in progress */
    int32_t *buf; uint32_t cap;                         /* channel-0 buffer: cap >= the largest block size accepted */
    uint32_t flags;
    f2_br_t br; f2_res_t rs; f2_pred_t pr;
    uint16_t crc;
    uint8_t phase, ch, nch, wasted, sub_type, order, err, frame_done, frame_ok;
    uint32_t bps_sub, i, emit_i;
    int32_t warm[32], cval, tmp[F2_RS];
    uint32_t frames_ok, frames_bad;
} f2_dec_t;

static inline void f2_dec_init(f2_dec_t *d, const f2_info_t *si, int32_t *buf, uint32_t cap, uint32_t flags)
{
    uint8_t *z = (uint8_t *)d; for (uint32_t i = 0; i < sizeof *d; i++) z[i] = 0;
    d->si = *si; d->buf = buf; d->cap = cap; d->flags = flags; d->phase = F2_PH_HDR;
}
/* Predicts samples from..to-1 of the buffered channel: the unit when it is running the subframe, else software. A fault finishes THIS sample in software (buf[i] still holds its residual) and turns the unit off. */
static inline void f2_pred_block(f2_dec_t *d, uint32_t from, uint32_t to)
{
    if (d->hw_on) {
        for (uint32_t i = from; i < to; i++) {
            int ok = 0; int32_t sm = d->hw->sample(d->buf[i], &ok);
            if (!ok) { d->hw_on = 0; d->hw = 0; d->hw_fails++; f2_pred_run(&d->pr, d->buf, i, to); return; }
#if BUG == 51
            d->buf[i] = sm + (int32_t)(i & 1u);                                       /* a corrupted unit answer */
#else
            d->buf[i] = sm;
#endif
            d->hw_samples++;
        }
    } else f2_pred_run(&d->pr, d->buf, from, to);
}
/* The stereo decorrelation of RFC 9639 section 4.2 / 9.2.2. a = channel 0, s = channel 1. */
static inline void f2_pair(uint32_t assign, int32_t a, int32_t s, int32_t *l, int32_t *r)
{
    if (assign == F2_CH_LEFT_SIDE) { *l = a; *r = (int32_t)((uint32_t)a - (uint32_t)s); }
    else if (assign == F2_CH_SIDE_RIGHT) { *r = s; *l = (int32_t)((uint32_t)s + (uint32_t)a); }
    else if (assign == F2_CH_MID_SIDE) {
#if BUG == 12
        int32_t mid = (int32_t)((uint32_t)a << 1);                  /* the dropped low bit of the sum */
#else
        int32_t mid = (int32_t)(((uint32_t)a << 1) | ((uint32_t)s & 1u));
#endif
        *l = (int32_t)((uint32_t)mid + (uint32_t)s) >> 1; *r = (int32_t)((uint32_t)mid - (uint32_t)s) >> 1;
    } else { *l = a; *r = s; }
}
/* Unary run of zeros ended by a 1, capped; atomic (reader restored on NEED/ERR). Returns F2_OK with *q, F2_NEED or F2_ERR. */
static inline int f2_unary(f2_br_t *b, uint32_t cap, uint32_t *q)
{
    f2_br_t s = *b; uint32_t n = 0, bit;
    for (;;) {
        if (!f2_get(b, 1, &bit)) { *b = s; return F2_NEED; }
        if (bit) break;
        if (++n > cap) { *b = s; return F2_ERR; }
    }
    *q = n; return F2_OK;
}

/* Returns the unused whole bytes of the reservoir to the window and leaves fewer than 8 bits in it, so that the bytes consumed so far are exactly [in, br.p). */
static inline void f2_giveback(f2_br_t *b)
{
    uint32_t g = b->nb >> 3; b->p -= g; b->nb &= 7u;
    b->w = b->nb ? (b->w & (0xFFFFFFFFu << (32u - b->nb))) : 0u;
}

/* After an error: drop the frame in progress (the reservoir, the CRC) so the next call parses a header at the first byte of its window. */
static inline void f2_dec_abort(f2_dec_t *d) { d->br.w = 0; d->br.nb = 0; d->crc = 0; d->phase = F2_PH_HDR; }

/* One step. `in`/`avail`: the unconsumed input window. `out`: room for `outcap` int32 L/R pairs. `maxres`: how many residuals this call may decode (the budget; at least 1).
 * Returns F2_OK (progress; call again), F2_NEED (give a larger window; nothing was lost) or F2_ERR (d->err says why; the frame is dead, resync from the next byte). *used = bytes consumed,
 * *npairs = pairs written. d->frame_done is set when this call finished a frame (d->frame_ok: its CRC-16 matched, or the check is off). Pairs belong to the frame in progress: the caller
 * stages them and commits them only when frame_done && frame_ok. */
static inline int f2_dec_step(f2_dec_t *d, const uint8_t *in, uint32_t avail, int32_t *out, uint32_t outcap, uint32_t maxres, uint32_t *used, uint32_t *npairs)
{
    f2_br_t *b = &d->br; int st = F2_OK; uint32_t np = 0, budget = maxres; int folded = 0;
    b->p = in; b->end = in + avail; d->frame_done = 0; d->err = F2E_NONE;
    for (;;) {
        if (d->phase == F2_PH_HDR) {
            f2_fh_t h; int r = f2_frame_header(b->p, (uint32_t)(b->end - b->p), &d->si, &h);
            if (r == F2_NEED) { st = F2_NEED; break; }
            if (r == F2_ERR) { d->err = h.err; st = F2_ERR; break; }
            if (h.chans > 2u) { d->err = F2E_UNSUPPORTED; st = F2_ERR; break; }
            if (h.bs > d->cap) { d->err = F2E_BLOCKSIZE; st = F2_ERR; break; }
            d->fh = h; b->p += h.hdr_len; d->ch = 0; d->nch = h.chans; d->phase = F2_PH_SUB;
        } else if (d->phase == F2_PH_SUB) {
            f2_br_t sv = *b; uint32_t zb, ty, wf, q = 0;
            if (!f2_get(b, 1, &zb) || !f2_get(b, 6, &ty) || !f2_get(b, 1, &wf)) { *b = sv; st = F2_NEED; break; }
            if (wf) { int r = f2_unary(b, 64u, &q); if (r != F2_OK) { *b = sv; if (r == F2_ERR) { d->err = F2E_SUBFRAME; } st = r; break; } q++; }
#if BUG == 17
            uint32_t bch = d->fh.bps;                                           /* the side channel's extra bit forgotten */
#else
            uint32_t bch = d->fh.bps + ((d->ch == 0 && d->fh.assign == F2_CH_SIDE_RIGHT) || (d->ch == 1 && (d->fh.assign == F2_CH_LEFT_SIDE || d->fh.assign == F2_CH_MID_SIDE)) ? 1u : 0u);
#endif
            if (zb || q >= bch) { d->err = F2E_SUBFRAME; st = F2_ERR; break; }
            d->wasted = (uint8_t)q; d->bps_sub = bch - q; d->sub_type = (uint8_t)ty;
            if (ty == 0u || ty == 1u) d->order = 0;
            else if (ty >= 8u && ty <= 12u) d->order = (uint8_t)(ty - 8u);
            else if (ty >= 32u) d->order = (uint8_t)(ty - 31u);
            else { d->err = F2E_SUBFRAME; st = F2_ERR; break; }             /* reserved types 2..7 and 13..31 */
            d->phase = F2_PH_SETUP;
        } else if (d->phase == F2_PH_SETUP) {
            f2_br_t sv = *b; uint32_t v, k, bp = d->bps_sub, order = d->order, ty = d->sub_type; int ok = 1; d->hw_on = 0;
            if (ty == 0u) { ok = f2_get(b, bp, &v); d->cval = (int32_t)f2_sext(v, bp); }
            else if (ty >= 8u) {
                for (k = 0; k < order && ok; k++) { ok = f2_get(b, bp, &v); d->warm[k] = (int32_t)f2_sext(v, bp); }
                if (ok && ty <= 12u) { f2_pred_fixed_set(&d->pr, order); }
                else if (ok) {
                    uint32_t prec, sh; int32_t coef[32];
                    ok = f2_get(b, 4, &prec) && f2_get(b, 5, &sh);
                    if (ok) {
                        prec += 1u;
                        if (prec == 16u || (sh & 16u)) { d->err = F2E_SUBFRAME; st = F2_ERR; break; }     /* precision 16 is invalid; a negative shift too */
                        for (k = 0; k < order && ok; k++) { ok = f2_get(b, prec, &v); coef[k] = (int32_t)f2_sext(v, prec); }
                        if (ok) {
                            f2_pred_lpc_set(&d->pr, order, sh, coef, bp, 0);
#if BUG == 52
                            d->hw_on = d->hw ? (uint8_t)(d->hw->begin(order, (int32_t)sh, coef, d->warm) ? 1 : 0) : 0;                   /* the unit also takes the 32-bit tier */
#else
                            d->hw_on = (d->hw && d->pr.tier == F2_TIER_64) ? (uint8_t)(d->hw->begin(order, (int32_t)sh, coef, d->warm) ? 1 : 0) : 0u;
#endif
                        }
                    }
                }
                if (ok) {
                    int r = f2_res_begin(b, &d->rs, d->fh.bs, order);
                    if (r == F2_ERR) { d->err = F2E_RESIDUAL; st = F2_ERR; break; }
                    ok = (r == F2_OK);
                }
            }
            if (!ok) { *b = sv; st = F2_NEED; break; }
            if (d->ch == 0) for (k = 0; k < order; k++) d->buf[k] = d->warm[k];       /* the buffered channel keeps its warm-up in place */
            d->i = (ty >= 8u && d->ch == 0u) ? order : 0u;                       /* the buffered channel keeps its warm-up in place; the streaming channel emits it first */
            d->phase = F2_PH_BODY;
        } else if (d->phase == F2_PH_BODY) {
            uint32_t bs = d->fh.bs, ty = d->sub_type, order = d->order, ws = d->wasted, bp = d->bps_sub;
            if (d->ch == 0u) {                                                  /* the buffered channel: decode into buf[], predict in place */
                while (d->i < bs) {
                    if (budget == 0u) { st = F2_OK; goto leave; }
                    uint32_t m = bs - d->i; if (m > budget) m = budget; uint32_t done = 0; int r = F2_OK, i0 = (int)d->i;
                    if (ty == 0u) { for (uint32_t k = 0; k < m; k++) d->buf[d->i + k] = d->cval; done = m; }
                    else if (ty == 1u) { for (; done < m; done++) { uint32_t v; if (!f2_get(b, bp, &v)) { r = F2_NEED; break; } d->buf[d->i + done] = (int32_t)f2_sext(v, bp); } }
                    else { r = f2_res_run(b, &d->rs, d->buf + d->i, m, &done); if (done) f2_pred_block(d, d->i, d->i + done); }
                    (void)i0; d->i += done; budget -= done;
                    if (r == F2_ERR) { d->err = F2E_RESIDUAL; st = F2_ERR; goto leave; }
                    if (r == F2_NEED) { st = F2_NEED; goto leave; }
                }
                if (ty >= 8u && !f2_res_done(&d->rs)) { d->err = F2E_RESIDUAL; st = F2_ERR; break; }
#if BUG == 13
                if (0) for (uint32_t k = 0; k < bs; k++) d->buf[k] = (int32_t)((uint32_t)d->buf[k] << ws);      /* wasted bits of the buffered channel never restored */
#else
                if (ws) for (uint32_t k = 0; k < bs; k++) d->buf[k] = (int32_t)((uint32_t)d->buf[k] << ws);
#endif
                if (d->nch == 1u) { d->phase = F2_PH_EMIT; d->emit_i = 0; } else { d->ch = 1; d->phase = F2_PH_SUB; }
            } else {                                                            /* the streaming channel: residual slice -> prediction -> decorrelate -> output pair, one after the other */
                while (d->i < bs) {
                    if (np == outcap || budget == 0u) { st = F2_OK; goto leave; }
                    uint32_t m = bs - d->i, done = 0; int r = F2_OK;
                    if (m > F2_RS) m = F2_RS; if (m > outcap - np) m = outcap - np;
                    if (ty >= 8u && d->i < order) {                             /* warm-up samples: no bits to read */
                        if (m > order - d->i) m = order - d->i;
                        for (; done < m; done++) d->tmp[done] = d->warm[d->i + done];
                    } else {
                        if (m > budget) m = budget;
                        if (ty == 0u) { for (; done < m; done++) d->tmp[done] = d->cval; }
                        else if (ty == 1u) { for (; done < m; done++) { uint32_t v; if (!f2_get(b, bp, &v)) { r = F2_NEED; break; } d->tmp[done] = (int32_t)f2_sext(v, bp); } }
                        else r = f2_res_run(b, &d->rs, d->tmp, m, &done);
                        budget -= done;
                    }
                    for (uint32_t k = 0; k < done; k++) {
                        uint32_t idx = d->i + k; int32_t u = d->tmp[k];
                        if (ty >= 8u && idx >= order) {
                            if (d->hw_on) {
                                int ok = 0; int32_t sm = d->hw->sample(u, &ok);
                                if (ok) { u = sm; d->hw_samples++; }
                                else { d->hw_on = 0; d->hw = 0; d->hw_fails++; u = (int32_t)((uint32_t)u + f2_pred_at(&d->pr, d->buf, idx, ws)); }
                            } else u = (int32_t)((uint32_t)u + f2_pred_at(&d->pr, d->buf, idx, ws));
                        }
#if BUG == 15
                        int32_t sv2 = u, L, R;                                      /* wasted bits of the streaming channel ignored */
#else
                        int32_t sv2 = (int32_t)((uint32_t)u << ws), L, R;
#endif
                        f2_pair(d->fh.assign, d->buf[idx], sv2, &L, &R);
                        out[2u * np] = L; out[2u * np + 1u] = R; np++; d->buf[idx] = sv2;
                    }
                    d->i += done;
                    if (r == F2_ERR) { d->err = F2E_RESIDUAL; st = F2_ERR; goto leave; }
                    if (r == F2_NEED) { st = F2_NEED; goto leave; }
                }
                if (ty >= 8u && !f2_res_done(&d->rs)) { d->err = F2E_RESIDUAL; st = F2_ERR; break; }
                d->phase = F2_PH_TRAILER;
            }
        } else if (d->phase == F2_PH_EMIT) {                                    /* mono: the buffered channel is the output */
            while (d->emit_i < d->fh.bs && np < outcap) { int32_t v = d->buf[d->emit_i++]; out[2u * np] = v; out[2u * np + 1u] = v; np++; }
            if (d->emit_i < d->fh.bs) { st = F2_OK; break; }
            d->phase = F2_PH_TRAILER;
        } else {                                                                /* F2_PH_TRAILER: pad to a byte, then the CRC-16 */
#if BUG == 16
            f2_giveback(b);                                                     /* no padding to the byte boundary */
#else
            f2_drop(b, b->nb & 7u); f2_giveback(b);
#endif
            if (b->end - b->p < 2) { st = F2_NEED; break; }
            uint16_t want = (uint16_t)(((uint16_t)b->p[0] << 8) | b->p[1]); (void)want;
            if (d->flags & F2_FLAG_CRC16) d->crc = f2_crc16(d->crc, in, (uint32_t)(b->p - in));
#if BUG == 14
            int ok = 1;                                                         /* the CRC-16 is never checked */
#else
            int ok = !(d->flags & F2_FLAG_CRC16) || d->crc == want;
#endif
            b->p += 2; folded = 1; d->crc = 0; d->frame_done = 1; d->frame_ok = (uint8_t)ok; d->phase = F2_PH_HDR;
            if (ok) { d->frames_ok++; st = F2_OK; } else { d->frames_bad++; d->err = F2E_CRC16; st = F2_ERR; }
            break;
        }
    }
leave:
    if (!folded) { f2_giveback(b); if (d->flags & F2_FLAG_CRC16) d->crc = f2_crc16(d->crc, in, (uint32_t)(b->p - in)); }
    *used = (uint32_t)(b->p - in); *npairs = np;
    return st;
}


/* ---- finding a frame, the sample timeline, and recovery (spec 5.6, 5.7) ---- */
/* The first sample a frame carries: its coded number for a variable-block-size stream, number x block size for a fixed one. Returns 1 and *s; 0 when it cannot be known (a fixed stream whose STREAMINFO
 * block sizes differ: the timeline is not tracked); -1 when it lies past this player's 32-bit sample counts, which no real frame does: a false sync. */
static inline int f2_frame_sample(const f2_info_t *si, const f2_fh_t *h, uint32_t *s)
{
    if ((uint32_t)(h->number >> 32)) return -1;
    uint32_t n = (uint32_t)h->number;
    if (h->variable) { *s = n; return 1; }
    if (si->min_bs != si->max_bs) return 0;
    if (n > 0xFFFFFFFFu / si->min_bs) return -1;
    *s = n * si->min_bs; return 1;
}
typedef struct { uint32_t next; uint8_t have; } f2_tl_t;                  /* next = the first sample the next frame must carry */
#define F2_MAXGAP_SAMPLES 1048576u                                      /* a header claiming a start further ahead than this (about 22 s at 48 kHz) is taken for a false sync */

/* Scans in[from..avail) for a frame header that passes CRC-8 and matches STREAMINFO, has the expected blocking strategy (want_var: -1 any, 0 fixed, 1 variable) and, when the timeline is known,
 * starts at or soon after the expected sample. F2_OK: *off is its offset. F2_NEED: no candidate yet; *off = how many bytes can be thrown away (a header might straddle the window end). */
static inline int f2_find_frame(const uint8_t *in, uint32_t avail, uint32_t from, const f2_info_t *si, int want_var, const f2_tl_t *tl, uint32_t *off)
{
    for (uint32_t i = from; i + 1u < avail; i++) {
        if (in[i] != 0xFFu || (in[i + 1] & 0xFEu) != 0xF8u) continue;
        f2_fh_t h; int r = f2_frame_header(in + i, avail - i, si, &h);
        if (r == F2_NEED) { *off = i; return F2_NEED; }                   /* a valid prefix at the end of the window: keep it */
        if (r != F2_OK) continue;
        if (want_var >= 0 && (int)h.variable != want_var) continue;     /* the 0xFFF8 / 0xFFF9 sync codes must not mix within a stream */
        uint32_t smp;
#if BUG == 19
        (void)smp; (void)tl;                                             /* no timeline check: any valid header is taken */
#else
        { int fs = f2_frame_sample(si, &h, &smp); if (fs < 0) continue; if (tl && tl->have && fs > 0) { if (smp < tl->next || smp - tl->next > F2_MAXGAP_SAMPLES) continue; } }
#endif
        *off = i; return F2_OK;
    }
    *off = avail > 16u ? avail - 16u : 0u;                              /* the last 15 bytes could start a header */
    if (*off < from) *off = from;
    return F2_NEED;
}

enum { F2_EV_NONE = 0, F2_EV_FRAME_OK = 1, F2_EV_FRAME_BAD = 2, F2_EV_SILENCE = 4, F2_EV_GIVEUP = 8 };
enum { F2_RM_NORMAL = 0, F2_RM_SCAN = 1, F2_RM_DEAD = 2 };
#define F2_MAX_FAILS 8u                                                 /* consecutive failures before the segment is given up */
typedef struct {
    f2_dec_t d; f2_tl_t tl;
    uint8_t mode, want_var; uint32_t fails, skip_first, scanned;
    uint32_t owed;                                                      /* silent pairs still to deliver */
    uint32_t frame_errors, resyncs, silent_pairs;
    uint8_t in_frame;                                                   /* a frame's header has been accepted and its pairs are staged by the caller */
    uint8_t tl_pending;                                                 /* the timeline is unknown and this frame's start is on trial: only a verified frame may establish it */
    uint32_t frame_start_s, frame_bs; uint8_t frame_known;     /* the accepted frame's start sample and size, for the timeline */
} f2_rec_t;
/* The stream starts at a known sample (0 at the start of a track): lost audio from there on is owed as silence. After a seek the start is NOT known and the timeline is left to the first verified frame. */
static inline void f2_rec_set_timeline(f2_rec_t *r, uint32_t first_sample) { r->tl.have = 1; r->tl.next = first_sample; }
static inline void f2_rec_init(f2_rec_t *r, const f2_info_t *si, int32_t *buf, uint32_t cap, uint32_t flags)
{
    uint8_t *z = (uint8_t *)r; for (uint32_t i = 0; i < sizeof *r; i++) z[i] = 0;
    f2_dec_init(&r->d, si, buf, cap, flags); r->want_var = 0xFFu;
}
/* A recovery-aware step. Returns F2_OK, F2_NEED or F2_ERR (only for a give-up). *ev says what happened:
 *   F2_EV_NONE        pairs (np may be 0) belong to the frame in progress: STAGE them;
 *   F2_EV_FRAME_OK    the frame finished and verified: COMMIT the staged pairs (those of this call included);
 *   F2_EV_FRAME_BAD   the frame failed: DISCARD everything staged for it; its duration will arrive as silence;
 *   F2_EV_SILENCE     np zero pairs, to be committed at once (lost or damaged audio, keeps `put == timeline`);
 *   F2_EV_GIVEUP      too many consecutive failures or nothing found: end the segment.
 * `start_known` tells the first frame of a stream or after a seek: the timeline adopts its start sample instead of measuring a gap. */
static inline int f2_rec_step(f2_rec_t *r, const uint8_t *in, uint32_t avail, int32_t *out, uint32_t outcap, uint32_t maxres, uint32_t *used, uint32_t *np, int *ev)
{
    f2_dec_t *d = &r->d; *used = 0; *np = 0; *ev = F2_EV_NONE;
    if (r->mode == F2_RM_DEAD) { *ev = F2_EV_GIVEUP; return F2_ERR; }
    if (r->owed && !r->in_frame) {                                      /* silence first: it belongs before whatever comes next */
        uint32_t n = r->owed < outcap ? (uint32_t)r->owed : outcap;
        for (uint32_t i = 0; i < 2u * n; i++) out[i] = 0;
        r->owed -= n; r->silent_pairs += n; *np = n; *ev = F2_EV_SILENCE; return F2_OK;
    }
    if (r->mode == F2_RM_SCAN) {
        uint32_t off = 0, from = r->skip_first; r->skip_first = 0;
        int rc = f2_find_frame(in, avail, from, &d->si, r->want_var == 0xFFu ? -1 : (int)r->want_var, &r->tl, &off);
        *used = off; r->scanned += off;
        if (rc == F2_OK) { r->mode = F2_RM_NORMAL; r->resyncs++; r->scanned = 0; f2_dec_abort(d); return F2_OK; }
        if (r->scanned > 262144u) { r->mode = F2_RM_DEAD; *ev = F2_EV_GIVEUP; return F2_ERR; }
        return F2_NEED;
    }
    if (d->phase == F2_PH_HDR && !r->in_frame) {                        /* a frame is about to start: look at its header first for the timeline */
        f2_fh_t h; int rc = f2_frame_header(in, avail, &d->si, &h);
        if (rc == F2_NEED) return F2_NEED;
        if (rc == F2_ERR || h.chans > 2u || h.bs > d->cap) {            /* not a frame: scan from the next byte */
            r->frame_errors++; r->fails++; if (r->fails > F2_MAX_FAILS) { r->mode = F2_RM_DEAD; *ev = F2_EV_GIVEUP; return F2_ERR; }
            r->mode = F2_RM_SCAN; r->skip_first = 1; return F2_OK;
        }
        uint32_t smp; int fs = f2_frame_sample(&d->si, &h, &smp); r->frame_known = fs > 0;
        if (fs < 0) { r->frame_errors++; r->fails++; if (r->fails > F2_MAX_FAILS) { r->mode = F2_RM_DEAD; *ev = F2_EV_GIVEUP; return F2_ERR; } r->mode = F2_RM_SCAN; r->skip_first = 1; return F2_OK; }       /* a start past 32 bits: a false sync */
        if (r->want_var == 0xFFu) r->want_var = h.variable;
        else if (r->want_var != h.variable) { r->frame_errors++; r->fails++; r->mode = F2_RM_SCAN; r->skip_first = 1; return F2_OK; }
        if (r->frame_known) {
#if BUG == 31
            if (!r->tl.have) { r->tl.have = 1; r->tl.next = smp; }                 /* an unverified header sets the timeline */
#else
            if (!r->tl.have) { r->tl_pending = 1; }
#endif
#if BUG == 19
            else if (smp > r->tl.next) { r->owed = smp - r->tl.next; r->tl.next = smp; return F2_OK; }
#else
            else if (smp > r->tl.next + F2_MAXGAP_SAMPLES) { r->frame_errors++; r->fails++; if (r->fails > F2_MAX_FAILS) { r->mode = F2_RM_DEAD; *ev = F2_EV_GIVEUP; return F2_ERR; } r->mode = F2_RM_SCAN; r->skip_first = 1; return F2_OK; }   /* a start that far ahead is a false sync, not 22 seconds of lost audio */
            else if (smp > r->tl.next) { r->owed = smp - r->tl.next; r->tl.next = smp; return F2_OK; }          /* the lost audio goes out first */
#endif
            else if (smp < r->tl.next) { r->frame_errors++; r->fails++; r->mode = F2_RM_SCAN; r->skip_first = 1; return F2_OK; }   /* behind the timeline: not ours */
            r->frame_start_s = smp;
        }
        r->frame_bs = h.bs; r->in_frame = 1;
    }
    int st = f2_dec_step(d, in, avail, out, outcap, maxres, used, np);
    if (d->frame_done && d->frame_ok) {
#if BUG == 21
        *ev = F2_EV_FRAME_OK; r->in_frame = 0; if (r->frame_known)       /* the failure counter is never cleared */
#else
        *ev = F2_EV_FRAME_OK; r->in_frame = 0; r->fails = 0; if (r->frame_known)
#endif
        { r->tl.have = 1; r->tl_pending = 0; r->tl.next = r->frame_start_s + r->frame_bs; }
        return F2_OK;
    }
    if (st == F2_ERR) {                                                 /* the frame failed: its pairs so far are void, its duration is owed as silence, and we look for the next frame */
        *ev = F2_EV_FRAME_BAD; r->frame_errors++; r->fails++;
#if BUG == 18
        if (r->in_frame && r->tl.have) { if (r->frame_known) r->tl.next = r->frame_start_s + r->frame_bs; }       /* the damaged frame's duration is never owed */
#else
        if (r->in_frame && r->tl.have) { r->owed += r->frame_bs; if (r->frame_known) r->tl.next = r->frame_start_s + r->frame_bs; }
#endif
        r->tl_pending = 0;
        r->in_frame = 0; f2_dec_abort(d);
        if (r->fails > F2_MAX_FAILS) { r->mode = F2_RM_DEAD; *ev |= F2_EV_GIVEUP; return F2_ERR; }
        r->mode = F2_RM_SCAN; r->skip_first = 0;
        return F2_OK;
    }
    return st;
}


/* ---- opening a file: ID3v2 prefix, "fLaC", the metadata blocks (RFC 9639 section 8) ---- */
enum { F2_OP_START = 0, F2_OP_ID3, F2_OP_MAGIC, F2_OP_BHDR, F2_OP_BODY, F2_OP_SKIP, F2_OP_DONE };
typedef struct {
    uint8_t state, last, btype, hn, have_si, have_seek, have_vc, seek_trunc, vc_trunc, err;
    uint8_t hdr[10]; uint32_t blen, left, pos;                  /* pos: file offset of the next byte to be consumed */
    uint8_t si_raw[34]; f2_info_t si;
    uint8_t *seek_buf; uint32_t seek_cap, seek_len, seek_total, seek_off;     /* SEEKTABLE: the first seek_cap bytes, its total size, its file offset */
    uint8_t *vc_buf;   uint32_t vc_cap, vc_len, vc_total, vc_off;             /* VORBIS_COMMENT, same (ReplayGain tags live here) */
    uint32_t audio_off;                                                  /* the file offset of the first frame, once state == F2_OP_DONE */
} f2_open_t;
static inline void f2_open_init(f2_open_t *o, uint8_t *seek_buf, uint32_t seek_cap, uint8_t *vc_buf, uint32_t vc_cap)
{
    uint8_t *z = (uint8_t *)o; for (uint32_t i = 0; i < sizeof *o; i++) z[i] = 0;
    o->seek_buf = seek_buf; o->seek_cap = seek_cap; o->vc_buf = vc_buf; o->vc_cap = vc_cap;
}
/* Bytes the walker would only skip (a PICTURE, padding, an unwanted block): a caller that can move its stream cheaply may skip them without reading and then call f2_open_skipped(). */
static inline uint32_t f2_open_skippable(const f2_open_t *o) { return o->state == F2_OP_SKIP ? o->left : 0u; }
static inline int f2_open_finish_block(f2_open_t *o);
static inline int f2_open_skipped(f2_open_t *o, uint32_t n) { if (n > o->left) n = o->left; o->left -= n; o->pos += n; return o->left == 0u ? f2_open_finish_block(o) : F2_OK; }
static inline int f2_open_finish_block(f2_open_t *o)
{
    if (o->last) {
        if (!o->have_si || f2_streaminfo(o->si_raw, &o->si) != F2_OK) { o->err = 1; return F2_ERR; }
#if BUG == 25
        o->audio_off = o->pos - 1u; o->state = F2_OP_DONE;
#else
        o->audio_off = o->pos; o->state = F2_OP_DONE;
#endif
    } else { o->state = F2_OP_BHDR; o->hn = 0; }
    return F2_OK;
}
/* Consumes bytes from in[0..avail). F2_OK: done (o->state == F2_OP_DONE, o->audio_off valid) or progress made with more to come (call again with the following bytes); F2_NEED: needs more bytes (all of `in`
 * has been consumed); F2_ERR: not a FLAC file / malformed metadata (o->err). *used = bytes consumed. */
static inline int f2_open_step(f2_open_t *o, const uint8_t *in, uint32_t avail, uint32_t *used)
{
    uint32_t i = 0; *used = 0;
    while (i < avail && o->state != F2_OP_DONE) {
        uint8_t c = in[i];
        switch (o->state) {
        case F2_OP_START:
            o->hdr[o->hn++] = c; i++; o->pos++;
            if (o->hn >= 3u && o->hdr[0] == 'I' && o->hdr[1] == 'D' && o->hdr[2] == '3') { if (o->hn == 10u) {
                    if ((o->hdr[6] | o->hdr[7] | o->hdr[8] | o->hdr[9]) & 0x80u) { o->err = 2; *used = i; return F2_ERR; }
                    o->left = ((uint32_t)o->hdr[6] << 21) | ((uint32_t)o->hdr[7] << 14) | ((uint32_t)o->hdr[8] << 7) | o->hdr[9];
#if BUG != 23
                    if (o->hdr[5] & 0x10u) o->left += 10u;                                  /* a footer */
#endif
                    o->state = F2_OP_ID3; o->hn = 0; } }
            else if (o->hn == 4u) { if (o->hdr[0] == 'f' && o->hdr[1] == 'L' && o->hdr[2] == 'a' && o->hdr[3] == 'C') { o->state = F2_OP_BHDR; o->hn = 0; } else { o->err = 3; *used = i; return F2_ERR; } }
            break;
        case F2_OP_ID3: {                                                    /* skip the tag body, then expect the magic */
            uint32_t n = o->left < avail - i ? o->left : avail - i; i += n; o->left -= n; o->pos += n;
            if (o->left == 0u) { o->state = F2_OP_MAGIC; o->hn = 0; }
            break; }
        case F2_OP_MAGIC:
            o->hdr[o->hn++] = c; i++; o->pos++;
            if (o->hn == 4u) { if (o->hdr[0] == 'f' && o->hdr[1] == 'L' && o->hdr[2] == 'a' && o->hdr[3] == 'C') { o->state = F2_OP_BHDR; o->hn = 0; } else { o->err = 3; *used = i; return F2_ERR; } }
            break;
        case F2_OP_BHDR:
            o->hdr[o->hn++] = c; i++; o->pos++;
            if (o->hn == 4u) {
                o->last = o->hdr[0] >> 7; o->btype = o->hdr[0] & 0x7Fu; 
#if BUG == 22
                o->blen = ((uint32_t)o->hdr[2] << 8) | o->hdr[3];                       /* the top length byte ignored */
#else
                o->blen = ((uint32_t)o->hdr[1] << 16) | ((uint32_t)o->hdr[2] << 8) | o->hdr[3];
#endif
                o->hn = 0;
                if (o->btype == 127u || (!o->have_si && (o->btype != 0u || o->blen != 34u)) || (o->btype == 0u && (o->have_si || o->blen != 34u))) { o->err = 4; *used = i; return F2_ERR; }
                if (o->btype == 3u && o->blen % 18u) { o->err = 5; *used = i; return F2_ERR; }
                o->left = o->blen;
                if (o->btype == 3u) { o->have_seek = 1; o->seek_off = o->pos; o->seek_total = o->blen; o->seek_len = 0; }
                if (o->btype == 4u) { o->have_vc = 1; o->vc_off = o->pos; o->vc_total = o->blen; o->vc_len = 0; }
                if (o->blen == 0u) { if (f2_open_finish_block(o) != F2_OK) { *used = i; return F2_ERR; } }
                else o->state = (o->btype == 0u || o->btype == 3u || o->btype == 4u) ? F2_OP_BODY : F2_OP_SKIP;
            }
            break;
        case F2_OP_BODY: {
            uint32_t n = o->left < avail - i ? o->left : avail - i; const uint8_t *src = in + i;
            if (o->btype == 0u) { for (uint32_t k = 0; k < n; k++) o->si_raw[34u - o->left + k] = src[k]; if (o->left == n) o->have_si = 1; }
            else if (o->btype == 3u) { for (uint32_t k = 0; k < n; k++) { if (o->seek_len < o->seek_cap) o->seek_buf[o->seek_len++] = src[k];
#if BUG != 24
                    else o->seek_trunc = 1;
#endif
                     } }
            else { for (uint32_t k = 0; k < n; k++) { if (o->vc_len < o->vc_cap) o->vc_buf[o->vc_len++] = src[k]; else o->vc_trunc = 1; } }
            i += n; o->left -= n; o->pos += n;
            if (o->left == 0u && f2_open_finish_block(o) != F2_OK) { *used = i; return F2_ERR; }
            break; }
        default: {                                                           /* F2_OP_SKIP */
            uint32_t n = o->left < avail - i ? o->left : avail - i; i += n; o->left -= n; o->pos += n;
            if (o->left == 0u && f2_open_finish_block(o) != F2_OK) { *used = i; return F2_ERR; }
            break; }
        }
    }
    *used = i;
    return o->state == F2_OP_DONE ? F2_OK : F2_NEED;
}

/* ---- the SEEKTABLE (RFC 9639 section 8.5): 18-byte points { u64 first sample, u64 byte offset from the first frame, u16 samples in the frame }, placeholders (sample all ones) last ---- */
/* The last real point whose first sample is <= target. Returns 1 and fills the outputs, or 0 when there is none (no table, or the target precedes the first point's sample: use the first frame). */
static inline uint32_t f2_be32(const uint8_t *e) { return ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3]; }
static inline int f2_seekpoint(const uint8_t *tab, uint32_t points, uint32_t target, uint32_t *sample, uint32_t *offset, uint32_t *samples)
{
    int found = 0; uint32_t lo = 0, hi = points;                                   /* binary search over the real (non-placeholder) prefix; entries past 32 bits count as "after the target" */
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u; const uint8_t *e = tab + 18u * mid;
        if (f2_be32(e) != 0u) { hi = mid; continue; }                                 /* a sample number of 2^32 or more, or a placeholder (all ones) */
        uint32_t smp = f2_be32(e + 4);
        if (smp <= target && f2_be32(e + 8) == 0u) { lo = mid + 1u; found = 1; *sample = smp; *offset = f2_be32(e + 12); *samples = ((uint32_t)e[16] << 8) | e[17]; }
        else hi = mid;
    }
    return found;
}


/* ---- seeking without (or between) seek-table points: interpolate, probe, bracket (spec 5.7) ----
 * Offsets are bytes from the first frame. The bracket [lo, hi): lo is a frame start whose first sample is <= target (offset 0 / sample 0 to begin with, or a seek-table point), hi a frame start (or the
 * end of the audio) beyond the target. Each probe reads a window at the guess, takes the first plausible frame header in it (CRC-8, STREAMINFO match, the stream's blocking strategy, a start sample
 * strictly inside the bracket -- a false sync word carries a random number and fails that), and either lands (the frame holds the target) or shrinks the bracket. The caller then decodes from the found
 * frame with a recovery decoder whose timeline is unknown (f2_rec_*, no f2_rec_set_timeline): the first VERIFIED frame sets the timeline, and the pairs before the target are discarded. */
enum { F2_SEEK_CONT = 0, F2_SEEK_DONE = 1, F2_SEEK_FAIL = 2 };
#define F2_SEEK_WIN 4096u                                               /* bytes the caller reads per probe */
#define F2_SEEK_MAXPROBES 64u
typedef struct {
    uint32_t target, lo_off, lo_s, hi_off, hi_s, ceil, probe, run_start, found_off, found_s;
    uint32_t probes, avg_frame; uint8_t state; int8_t want_var;
} f2_seek_t;
static inline uint32_t f2_muldiv_approx(uint32_t a, uint32_t b, uint32_t c)       /* a*b/c in 32-bit arithmetic (a probe guess needs no more precision): c is brought under 2^16, then the product is built from 16-bit halves of b */
{
    while (c >> 16) { a >>= 1; c >>= 1; }
    if (c == 0u) return 0u;
    if (a > c) a = c;                                                           /* (a <= c in every use: a fraction of the span) */
    uint32_t q = (a << 16) / c;                                                 /* the fraction a/c in 16.16 */
    return (b >> 16) * q + (((b & 0xFFFFu) * q) >> 16);
}/* Chooses the next probe offset p with lo_off < p < ceil (ceil <= hi_off bounds where an undiscovered frame start can still be). Returns 0 when there is no room left. */
static inline int f2_seek_aim(f2_seek_t *k)
{
    if (k->ceil <= k->lo_off + 1u) return 0;
    uint32_t p;
    if (k->hi_off - k->lo_off <= F2_SEEK_WIN) p = k->lo_off + 1u;                  /* a bracket the size of a window: read right after the frame we know and walk forward through its successors */
    else {
        uint32_t d = k->target - k->lo_s, span_s = k->hi_s - k->lo_s, span_o = k->hi_off - k->lo_off;
        p = k->lo_off + f2_muldiv_approx(d, span_o, span_s ? span_s : 1u);
        uint32_t back = span_s ? f2_muldiv_approx(k->avg_frame, span_o, span_s) : 0u;   /* aim one average frame early, so the first header found is the frame BEFORE or AT the target */
        p = p > back ? p - back : 0u;
        if (k->probes >= 4u && k->probes % 3u == 0u) p = k->lo_off + (k->ceil - k->lo_off) / 2u;   /* every third probe a plain bisection: the range at least halves, whatever the bitrate does */
    }
    if (p <= k->lo_off) p = k->lo_off + 1u;
    if (p >= k->ceil) p = k->ceil - 1u;
    k->probe = p; return 1;
}
/* total_samples: STREAMINFO's count (0 = unknown: the seek fails and the caller plays from the start). (lo_off, lo_s): a known frame start at or before the target, (0, 0) if none. audio_bytes: bytes from
 * the first frame to the end of the file. */
static inline int f2_seek_begin(f2_seek_t *k, const f2_info_t *si, uint32_t target, uint32_t total_samples, uint32_t audio_bytes, uint32_t lo_off, uint32_t lo_s, int want_var)
{
    uint8_t *z = (uint8_t *)k; for (uint32_t i = 0; i < sizeof *k; i++) z[i] = 0;
    k->want_var = (int8_t)want_var; k->avg_frame = si->max_bs ? si->max_bs : 4096u;
    if (total_samples == 0u || audio_bytes < 2u) { k->state = F2_SEEK_FAIL; return F2_SEEK_FAIL; }
    if (target >= total_samples) target = total_samples - 1u;
    k->target = target; k->lo_off = lo_off; k->lo_s = lo_s; k->hi_off = audio_bytes; k->hi_s = total_samples; k->ceil = audio_bytes;
    k->found_off = lo_off; k->found_s = lo_s;
    if (lo_s > target || !f2_seek_aim(k)) { k->state = F2_SEEK_DONE; return F2_SEEK_DONE; }
    return F2_SEEK_CONT;
}
/* Feed the window read at the probe offset (k->probe), `avail` bytes (up to F2_SEEK_WIN; fewer at the end of the file). One window is walked as far as it goes: every plausible header in it is classified
 * -- wholly before the target moves the lower bound up and the walk continues, the frame holding the target ends the search, wholly after ends the window. */
static inline int f2_seek_feed(f2_seek_t *k, const f2_info_t *si, const uint8_t *win, uint32_t avail)
{
    if (k->state != F2_SEEK_CONT) return (int)k->state;
    uint32_t from = 0; int progressed = 0; k->probes++;
    for (;;) {
        uint32_t off = 0; f2_fh_t h; uint32_t smp;
        if (f2_find_frame(win, avail, from, si, k->want_var, 0, &off) != F2_OK) break;
        if (f2_frame_header(win + off, avail - off, si, &h) != F2_OK || f2_frame_sample(si, &h, &smp) <= 0) { from = off + 1u; continue; }
        uint32_t aoff = k->probe + off;
#if BUG != 27
        if (smp < k->lo_s || smp >= k->hi_s || aoff <= k->lo_off || aoff >= k->hi_off) { from = off + 1u; continue; }         /* implausible: a false sync */
#endif
        if (smp <= k->target && k->target < smp + h.bs) { k->found_off = aoff; k->found_s = smp; k->state = F2_SEEK_DONE; return F2_SEEK_DONE; }
#if BUG == 29
        if (smp + h.bs > k->target) { k->lo_off = aoff; k->lo_s = smp; k->found_off = aoff; k->found_s = smp; progressed = 1; from = off + 1u; continue; }
        k->hi_off = aoff; k->hi_s = smp; progressed = 1; break;                                                               /* the sides swapped */
#elif BUG == 30
        if (smp + h.bs <= k->target) { k->lo_off = aoff; k->lo_s = smp; progressed = 1; from = off + 1u; continue; }       /* the fallback frame is not remembered */
        k->hi_off = aoff; k->hi_s = smp; progressed = 1; break;
#else
        if (smp + h.bs <= k->target) { k->lo_off = aoff; k->lo_s = smp; k->found_off = aoff; k->found_s = smp; progressed = 1; from = off + 1u; continue; }   /* wholly before the target */
        k->hi_off = aoff; k->hi_s = smp; progressed = 1; break;                                                               /* wholly after */
#endif
    }
    if (k->ceil > k->hi_off) k->ceil = k->hi_off;
    if (!progressed) {                                                  /* nothing usable in this window: step on, or, at the end of the range, look EARLIER (the frame starts before where this run began) */
        if (k->run_start == 0u) k->run_start = k->probe;
        uint32_t nxt = k->probe + (avail > 32u ? avail - 16u : 1u);
        if (nxt < k->ceil) { k->probe = nxt; return F2_SEEK_CONT; }
        k->ceil = k->run_start; k->run_start = 0u;
    } else k->run_start = 0u;
    if (k->probes >= F2_SEEK_MAXPROBES || !f2_seek_aim(k)) { k->state = F2_SEEK_DONE; return F2_SEEK_DONE; }    /* decode forward from the last frame known to precede the target */
    return F2_SEEK_CONT;
}


#endif
