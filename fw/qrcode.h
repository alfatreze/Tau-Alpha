/* Minimal QR Code encoder for the Tau diagnostic report (docs/TEST_SUITE_SPEC.md section 8).
 * Portable C, no libc, no 64-bit math, no malloc: byte mode, error level L, versions 1..38, mask chosen by the standard's penalty
 * rules (or forced, for tests). All working memory is a caller-supplied buffer (QR_BUF_LEN bytes) so the firmware can keep it in the
 * PSRAM window instead of fast RAM. Tested on the host (tools/host/qr_harness.c under tools/rv32sim.py) against the segno reference
 * for every version and mask, and by decoding the rendered image (sim/test_qr.py). Algorithm follows ISO/IEC 18004; structure after
 * Nayuki's public-domain-style reference layout. */
#ifndef TAU_QRCODE_H
#define TAU_QRCODE_H

#include <stdint.h>
/* Firmware builds define QR_FN (cold code) and QR_TBL (cold data) before including this file. */
#ifndef QR_FN
#define QR_FN static
#endif
#ifndef QR_TBL
#define QR_TBL
#endif
#include "qr_tables.h"

#define QR_MAXV       QR_MAXV_T
#define QR_SIZE(v)    (4u * (v) + 17u)
#define QR_PLANE(sz)  (((sz) * (sz) + 7u) >> 3)
#define QR_MAX_SIZE   QR_SIZE(QR_MAXV)
/* modules plane + function plane + (data codewords + final message) */
#define QR_BUF_LEN    (2u * QR_PLANE(QR_MAX_SIZE) + QR_CW_MAX)

QR_FN uint32_t qr_total_cw(uint32_t v) { return (uint32_t)qr_ecc[v][0] * qr_ecc[v][1] + (uint32_t)qr_ecc[v][3] * qr_ecc[v][4]; }
QR_FN uint32_t qr_data_cw(uint32_t v)  { return (uint32_t)qr_ecc[v][0] * qr_ecc[v][2] + (uint32_t)qr_ecc[v][3] * qr_ecc[v][5]; }

static inline int qr_bit(const uint8_t *p, uint32_t i) { return (p[i >> 3] >> (7u - (i & 7u))) & 1; }
static inline void qr_setbit(uint8_t *p, uint32_t i, int v)
{
    uint8_t m = (uint8_t)(0x80u >> (i & 7u));
    if (v) p[i >> 3] |= m; else p[i >> 3] &= (uint8_t)~m;
}

/* The finished symbol: buf holds the module plane first. */
static inline int qr_get(const uint8_t *buf, uint32_t size, int x, int y)
{
    if (x < 0 || y < 0 || (uint32_t)x >= size || (uint32_t)y >= size) return 0;
    return qr_bit(buf, (uint32_t)y * size + (uint32_t)x);
}

QR_FN uint8_t qr_gf_mul(uint8_t x, uint8_t y)
{
    uint32_t z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11Du);
        z ^= (uint32_t)((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

/* ---- drawing state (kept in one struct so the helpers take one pointer) ---- */
typedef struct { uint8_t *mod, *fn; uint32_t size; } qr_t;

QR_FN void qr_fn(qr_t *q, int x, int y, int dark)
{
    if (x < 0 || y < 0 || (uint32_t)x >= q->size || (uint32_t)y >= q->size) return;
    uint32_t i = (uint32_t)y * q->size + (uint32_t)x;
    qr_setbit(q->mod, i, dark);
    qr_setbit(q->fn, i, 1);
}

QR_FN void qr_finder(qr_t *q, int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            int dist = ax > ay ? ax : ay;
            qr_fn(q, cx + dx, cy + dy, dist != 2 && dist != 4);
        }
}

QR_FN void qr_align_pat(qr_t *q, int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
            qr_fn(q, cx + dx, cy + dy, (ax > ay ? ax : ay) != 1);
        }
}

QR_FN void qr_format(qr_t *q, uint32_t mask)
{
    uint32_t data = (QR_ECL_BITS << 3) | mask;              /* error level bits (L = 01) above the mask */
    uint32_t rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537u);
    uint32_t bits = ((data << 10) | rem) ^ 0x5412u;
    int sz = (int)q->size;
    for (int i = 0; i <= 5; i++) qr_fn(q, 8, i, (bits >> i) & 1);
    qr_fn(q, 8, 7, (bits >> 6) & 1);
    qr_fn(q, 8, 8, (bits >> 7) & 1);
    qr_fn(q, 7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) qr_fn(q, 14 - i, 8, (bits >> i) & 1);
    for (int i = 0; i < 8; i++) qr_fn(q, sz - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++) qr_fn(q, 8, sz - 15 + i, (bits >> i) & 1);
    qr_fn(q, 8, sz - 8, 1);                                 /* the always-dark module */
}

QR_FN void qr_version_info(qr_t *q, uint32_t v)
{
    if (v < 7u) return;
    uint32_t rem = v;
    for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25u);
    uint32_t bits = (v << 12) | rem;
    for (int i = 0; i < 18; i++) {
        int b = (bits >> i) & 1;
        int a = (int)q->size - 11 + i % 3, c = i / 3;
        qr_fn(q, a, c, b);
        qr_fn(q, c, a, b);
    }
}

QR_FN void qr_function_patterns(qr_t *q, uint32_t v)
{
    int sz = (int)q->size;
    for (int i = 0; i < sz; i++) { qr_fn(q, 6, i, i % 2 == 0); qr_fn(q, i, 6, i % 2 == 0); }
    qr_finder(q, 3, 3); qr_finder(q, sz - 4, 3); qr_finder(q, 3, sz - 4);
    const uint8_t *pos = qr_align[v];
    int n = 0;
    while (n < 7 && pos[n]) n++;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (!((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))) qr_align_pat(q, pos[i], pos[j]);
    qr_format(q, 0);                                        /* reserves the format areas; redrawn with the real mask */
    qr_version_info(q, v);
}

/* ---- Reed-Solomon over GF(256), polynomial 0x11D ---- */
QR_FN void qr_rs_divisor(uint8_t *out, uint32_t deg)
{
    for (uint32_t i = 0; i < deg; i++) out[i] = 0;
    out[deg - 1] = 1;
    uint8_t root = 1;
    for (uint32_t i = 0; i < deg; i++) {
        for (uint32_t j = 0; j < deg; j++) {
            out[j] = qr_gf_mul(out[j], root);
            if (j + 1 < deg) out[j] ^= out[j + 1];
        }
        root = qr_gf_mul(root, 0x02);
    }
}

QR_FN void qr_rs_remainder(const uint8_t *data, uint32_t len, const uint8_t *div, uint32_t deg, uint8_t *res)
{
    for (uint32_t i = 0; i < deg; i++) res[i] = 0;
    for (uint32_t k = 0; k < len; k++) {
        uint8_t f = data[k] ^ res[0];
        for (uint32_t i = 0; i + 1 < deg; i++) res[i] = res[i + 1];
        res[deg - 1] = 0;
        for (uint32_t i = 0; i < deg; i++) res[i] ^= qr_gf_mul(div[i], f);
    }
}

QR_FN int qr_mask_bit(uint32_t m, uint32_t x, uint32_t y)
{
    switch (m) {
    case 0: return ((x + y) & 1u) == 0;
    case 1: return (y & 1u) == 0;
    case 2: return x % 3u == 0;
    case 3: return (x + y) % 3u == 0;
    case 4: return ((x / 3u + y / 2u) & 1u) == 0;
    case 5: return ((x * y) & 1u) + (x * y) % 3u == 0;
    case 6: return ((((x * y) & 1u) + (x * y) % 3u) & 1u) == 0;
    default: return (((x + y) & 1u) + (x * y) % 3u) % 2u == 0;
    }
}

QR_FN void qr_apply_mask(qr_t *q, uint32_t m)
{
    for (uint32_t y = 0; y < q->size; y++)
        for (uint32_t x = 0; x < q->size; x++) {
            uint32_t i = y * q->size + x;
            if (!qr_bit(q->fn, i) && qr_mask_bit(m, x, y)) qr_setbit(q->mod, i, !qr_bit(q->mod, i));
        }
}

/* ISO 18004 section 7.8.3 penalty (rule 3 as the 11-module pattern with four light modules either side). */
QR_FN uint32_t qr_penalty(const qr_t *q)
{
    const int sz = (int)q->size;
    uint32_t pen = 0;
    for (int pass = 0; pass < 2; pass++) {                   /* rows, then columns */
        for (int a = 0; a < sz; a++) {
            int run = 1;
            uint32_t win = 0;
            for (int b = 0; b < sz; b++) {
                int cur = pass ? qr_get(q->mod, q->size, a, b) : qr_get(q->mod, q->size, b, a);
                if (b > 0) {
                    int prev = pass ? qr_get(q->mod, q->size, a, b - 1) : qr_get(q->mod, q->size, b - 1, a);
                    if (cur == prev) { run++; if (run == 5) pen += 3; else if (run > 5) pen += 1; }
                    else run = 1;
                }
                win = ((win << 1) | (uint32_t)cur) & 0x7FFu;
                if (b >= 10 && (win == 0x5D0u || win == 0x05Du)) pen += 40;   /* 10111010000 / 00001011101 */
            }
        }
    }
    for (int y = 0; y + 1 < sz; y++)
        for (int x = 0; x + 1 < sz; x++) {
            int c = qr_get(q->mod, q->size, x, y);
            if (c == qr_get(q->mod, q->size, x + 1, y) && c == qr_get(q->mod, q->size, x, y + 1) && c == qr_get(q->mod, q->size, x + 1, y + 1)) pen += 3;
        }
    uint32_t dark = 0, total = q->size * q->size;
    for (uint32_t i = 0; i < total; i++) dark += (uint32_t)qr_bit(q->mod, i);
    uint32_t d = dark * 20u > total * 10u ? dark * 20u - total * 10u : total * 10u - dark * 20u;
    uint32_t k = (d + total - 1u) / total;
    pen += (k ? k - 1u : 0u) * 10u;
    return pen;
}

/* Encodes `len` bytes. version search runs minver..maxver. force_mask 0..7 forces that mask (tests), -1 picks the best.
 * Returns the module count per side (0 if the data does not fit); buf must be QR_BUF_LEN bytes and is left holding the symbol
 * (module plane first: read it with qr_get). */
QR_FN uint32_t qr_encode(const uint8_t *data, uint32_t len, uint32_t minver, uint32_t maxver, int force_mask,
                          uint8_t *buf, uint32_t *ver_out, uint32_t *mask_out)
{
    uint32_t v = minver ? minver : 1u;
    for (; v <= maxver && v <= QR_MAXV; v++) {
        uint32_t cc = v < 10u ? 8u : 16u;
        if (4u + cc + 8u * len <= 8u * qr_data_cw(v)) break;
    }
    if (v > maxver || v > QR_MAXV) return 0;
    const uint32_t size = QR_SIZE(v), plane = QR_PLANE(size), dcw = qr_data_cw(v), tcw = qr_total_cw(v);
    uint8_t *cw = buf + 2u * plane;                          /* data codewords, then the interleaved final message */
    qr_t q = { buf, buf + plane, size };
    for (uint32_t i = 0; i < 2u * plane; i++) buf[i] = 0;
    for (uint32_t i = 0; i < dcw + tcw; i++) cw[i] = 0;

    /* 1. data bitstream: mode 0100, count, bytes, terminator, padding */
    uint32_t nb = 0;
    uint32_t cc = v < 10u ? 8u : 16u;
    for (int i = 3; i >= 0; i--) { qr_setbit(cw, nb++, (4u >> i) & 1u); }
    for (int i = (int)cc - 1; i >= 0; i--) qr_setbit(cw, nb++, (len >> i) & 1u);
    for (uint32_t k = 0; k < len; k++)
        for (int i = 7; i >= 0; i--) qr_setbit(cw, nb++, (data[k] >> i) & 1u);
    uint32_t cap = 8u * dcw;
    for (uint32_t t = 0; t < 4u && nb < cap; t++) qr_setbit(cw, nb++, 0);
    nb = (nb + 7u) & ~7u;
    for (uint32_t pad = 0xECu; nb < cap; nb += 8u, pad ^= 0xECu ^ 0x11u) cw[nb >> 3] = (uint8_t)pad;

    /* 2. error correction per block, interleaved */
    const uint32_t nblocks = (uint32_t)qr_ecc[v][0] + qr_ecc[v][3];
    const uint32_t ecc_len = (uint32_t)qr_ecc[v][1] - qr_ecc[v][2];
    uint8_t div[30], rem[30];
    qr_rs_divisor(div, ecc_len);
    uint8_t *fin = cw + dcw;
    uint32_t maxd = qr_ecc[v][3] ? qr_ecc[v][5] : qr_ecc[v][2];
    for (uint32_t b = 0; b < nblocks; b++) {
        uint32_t dl = b < qr_ecc[v][0] ? qr_ecc[v][2] : qr_ecc[v][5];
        uint32_t st = b < qr_ecc[v][0] ? b * qr_ecc[v][2] : (uint32_t)qr_ecc[v][0] * qr_ecc[v][2] + (b - qr_ecc[v][0]) * qr_ecc[v][5];
        qr_rs_remainder(cw + st, dl, div, ecc_len, rem);
        for (uint32_t i = 0; i < ecc_len; i++) fin[dcw + i * nblocks + b] = rem[i];
    }
    uint32_t w = 0;
    for (uint32_t i = 0; i < maxd; i++)
        for (uint32_t b = 0; b < nblocks; b++) {
            uint32_t dl = b < qr_ecc[v][0] ? qr_ecc[v][2] : qr_ecc[v][5];
            uint32_t st = b < qr_ecc[v][0] ? b * qr_ecc[v][2] : (uint32_t)qr_ecc[v][0] * qr_ecc[v][2] + (b - qr_ecc[v][0]) * qr_ecc[v][5];
            if (i < dl) fin[w++] = cw[st + i];
        }

    /* 3. function patterns, then the codewords in the zigzag */
    qr_function_patterns(&q, v);
    uint32_t bi = 0;
    for (int right = (int)size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (uint32_t vert = 0; vert < size; vert++)
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                int upward = ((right + 1) & 2) == 0;
                int y = upward ? (int)(size - 1u - vert) : (int)vert;
                uint32_t i = (uint32_t)y * size + (uint32_t)x;
                if (!qr_bit(q.fn, i) && bi < 8u * tcw) { qr_setbit(q.mod, i, qr_bit(fin, bi)); bi++; }
            }
    }

    /* 4. mask: forced, or the lowest penalty of the eight */
    int best = force_mask;
    if (best < 0) {
        uint32_t bestpen = 0xFFFFFFFFu;
        for (uint32_t m = 0; m < 8u; m++) {
            qr_apply_mask(&q, m); qr_format(&q, m);
            uint32_t p = qr_penalty(&q);
            qr_apply_mask(&q, m);                            /* undo */
            if (p < bestpen) { bestpen = p; best = (int)m; }
        }
    }
    qr_apply_mask(&q, (uint32_t)best);
    qr_format(&q, (uint32_t)best);
    if (ver_out) *ver_out = v;
    if (mask_out) *mask_out = (uint32_t)best;
    return size;
}

#endif
