/* TPG1 (Tau pixel grid) encoder core: portable, pure, no I/O. Format: tools/tpg.py docstring and docs/features/BARCODE_STUDY.md.
 * A record is shown as one RGB565 value per screen pixel; this file only answers "which value is pixel (x, y)" so the page
 * (fw/suite.inc, TAU_TPG) can write it through the SDRAM mailbox and a host test can compare every pixel with the Python codec. */
#ifndef TPG_CORE_H
#define TPG_CORE_H
#include <stdint.h>

#define TPG_W 400u
#define TPG_H 360u
#define TPG_HDR 16u
#define TPG_MODE_L 0u
#define TPG_MODE_R 1u
#define TPG_CELL 4u
#define TPG_GW (TPG_W / TPG_CELL)       /* 100 */
#define TPG_GH (TPG_H / TPG_CELL)       /* 90  */

#ifndef TPG_FN
#define TPG_FN static
#endif
#ifndef TPG_CRC32
/* the firmware defines TPG_CRC32 as sr_crc32 (fw/suite_core.h) so the loop exists once */
TPG_FN uint32_t tpg_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1u));
    }
    return ~c;
}
#define TPG_CRC32 tpg_crc32
#endif

typedef struct { const uint8_t *rec; uint32_t len, mode; uint8_t hdr[TPG_HDR]; } tpg_t;

/* Payload capacity in bytes for a mode (287,984 for L, 6,734 for R). */
TPG_FN uint32_t tpg_capacity(uint32_t mode)
{
    return (mode == TPG_MODE_L ? TPG_W * TPG_H * 2u : TPG_GW * TPG_GH * 6u / 8u) - TPG_HDR;
}

/* Returns 0 when the record does not fit the mode. */
TPG_FN int tpg_init(tpg_t *t, const uint8_t *rec, uint32_t len, uint32_t mode)
{
    if (mode > TPG_MODE_R || len > tpg_capacity(mode)) return 0;
    uint32_t crc = TPG_CRC32(rec, len);
    t->rec = rec; t->len = len; t->mode = mode;
    t->hdr[0] = 'T'; t->hdr[1] = 'P'; t->hdr[2] = 'G'; t->hdr[3] = '1';
    t->hdr[4] = (uint8_t)mode; t->hdr[5] = 0; t->hdr[6] = 0; t->hdr[7] = 0;
    for (int k = 0; k < 4; k++) { t->hdr[8 + k] = (uint8_t)(len >> (24 - 8 * k)); t->hdr[12 + k] = (uint8_t)(crc >> (24 - 8 * k)); }
    return 1;
}

/* Pixel rows the stream occupies: ceil(ceil(n/2)/400) in mode L, ceil(ceil(n*8/6)/100)*4 in mode R, n = 16 + len. */
TPG_FN uint32_t tpg_rows(const tpg_t *t)
{
    uint32_t n = TPG_HDR + t->len;
    if (t->mode == TPG_MODE_L) return (((n + 1u) / 2u) + TPG_W - 1u) / TPG_W;
    return (((n * 8u + 5u) / 6u + TPG_GW - 1u) / TPG_GW) * TPG_CELL;
}

TPG_FN uint32_t tpg_byte(const tpg_t *t, uint32_t i)
{
    if (i < TPG_HDR) return t->hdr[i];
    i -= TPG_HDR;
    return i < t->len ? t->rec[i] : 0u;
}

/* The RGB565 value of screen pixel (x, y) for the stream; 0 past the end. */
TPG_FN uint16_t tpg_pixel(const tpg_t *t, uint32_t x, uint32_t y)
{
    if (t->mode == TPG_MODE_L) {
        uint32_t i = 2u * (y * TPG_W + x);
        return (uint16_t)((tpg_byte(t, i) << 8) | tpg_byte(t, i + 1u));
    }
    static const uint8_t l5[4] = { 0, 10, 21, 31 }, l6[4] = { 0, 21, 42, 63 };
    uint32_t bit = 6u * ((y / TPG_CELL) * TPG_GW + x / TPG_CELL);          /* first stream bit of this cell */
    uint32_t byte = bit >> 3, sh = bit & 7u;
    uint32_t v = (tpg_byte(t, byte) << 8) | tpg_byte(t, byte + 1u);        /* 16 bits holding the 6 we need */
    v = (v >> (10u - sh)) & 63u;
    return (uint16_t)((l5[(v >> 4) & 3u] << 11) | (l6[(v >> 2) & 3u] << 5) | l5[v & 3u]);
}
#endif
