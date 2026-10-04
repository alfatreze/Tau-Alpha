/* TPG2 (Tau pixel grid) encoder core: portable, pure, no I/O. Format: tools/tpg.py docstring and docs/features/BARCODE_STUDY.md.
 * A record is shown as a square block of RGB565 values, centred on the 400x360 screen and as small as the report allows; this file only
 * answers "where is the block" and "which value is pixel (x, y)" so the page (fw/suite.inc, TAU_TPG) can write it through the SDRAM mailbox
 * and a host test can compare every pixel with the Python codec. */
#ifndef TPG_CORE_H
#define TPG_CORE_H
#include <stdint.h>

#define TPG_W 400u
#define TPG_H 360u
#define TPG_HDR 16u
#define TPG_MODE_L 0u
#define TPG_MODE_R 1u
#define TPG_CELL 4u

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

/* Block sides (tools/tpg.py LADDER_L / LADDER_R): pixels for mode L, 4x4-pixel cells for mode R. */
static const uint16_t tpg_ladder_l[12] = { 64, 80, 96, 112, 128, 160, 192, 224, 256, 288, 320, 360 };
static const uint8_t  tpg_ladder_r[11] = { 20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 90 };

/* rec/len/mode, the 16-byte header, the block (x0, y0, px = side in pixels) and its side in stream units (pixels for L, cells for R). */
typedef struct { const uint8_t *rec; uint32_t len, mode, side, x0, y0, px; uint8_t hdr[TPG_HDR]; } tpg_t;

TPG_FN uint32_t tpg_units(uint32_t side, uint32_t mode)       /* stream bytes a block holds, header included */
{
    return mode == TPG_MODE_L ? side * side * 2u : side * side * 6u / 8u;
}

/* Payload capacity in bytes for a mode (259,184 for L, 6,059 for R). */
TPG_FN uint32_t tpg_capacity(uint32_t mode)
{
    return tpg_units(mode == TPG_MODE_L ? tpg_ladder_l[11] : tpg_ladder_r[10], mode) - TPG_HDR;
}

/* Returns 0 when the record does not fit the mode. Picks the smallest ladder step that holds it. */
TPG_FN int tpg_init(tpg_t *t, const uint8_t *rec, uint32_t len, uint32_t mode)
{
    if (mode > TPG_MODE_R || len > tpg_capacity(mode)) return 0;
    uint32_t side = 0;
    for (uint32_t i = 0; i < (mode == TPG_MODE_L ? 12u : 11u); i++) {
        side = mode == TPG_MODE_L ? tpg_ladder_l[i] : tpg_ladder_r[i];
        if (tpg_units(side, mode) >= TPG_HDR + len) break;
    }
    uint32_t crc = TPG_CRC32(rec, len);
    t->rec = rec; t->len = len; t->mode = mode; t->side = side;
    t->px = mode == TPG_MODE_L ? side : side * TPG_CELL;
    t->x0 = (TPG_W - t->px) / 2u; t->y0 = (TPG_H - t->px) / 2u;
    t->hdr[0] = 'T'; t->hdr[1] = 'P'; t->hdr[2] = 'G'; t->hdr[3] = '2';
    t->hdr[4] = (uint8_t)mode; t->hdr[5] = 0; t->hdr[6] = 0; t->hdr[7] = 0;
    for (int k = 0; k < 4; k++) { t->hdr[8 + k] = (uint8_t)(len >> (24 - 8 * k)); t->hdr[12 + k] = (uint8_t)(crc >> (24 - 8 * k)); }
    return 1;
}

TPG_FN uint32_t tpg_byte(const tpg_t *t, uint32_t i)
{
    if (i < TPG_HDR) return t->hdr[i];
    i -= TPG_HDR;
    return i < t->len ? t->rec[i] : 0u;
}

/* The RGB565 value of SCREEN pixel (x, y), which must lie inside the block [x0, x0 + px) x [y0, y0 + px). */
TPG_FN uint16_t tpg_pixel(const tpg_t *t, uint32_t x, uint32_t y)
{
    const uint32_t lx = x - t->x0, ly = y - t->y0;
    if (t->mode == TPG_MODE_L) {
        uint32_t i = 2u * (ly * t->side + lx);
        return (uint16_t)((tpg_byte(t, i) << 8) | tpg_byte(t, i + 1u));
    }
    static const uint8_t l5[4] = { 0, 10, 21, 31 }, l6[4] = { 0, 21, 42, 63 };
    uint32_t bit = 6u * ((ly / TPG_CELL) * t->side + lx / TPG_CELL);       /* first stream bit of this cell */
    uint32_t byte = bit >> 3, sh = bit & 7u;
    uint32_t v = (tpg_byte(t, byte) << 8) | tpg_byte(t, byte + 1u);        /* 16 bits holding the 6 we need */
    v = (v >> (10u - sh)) & 63u;
    return (uint16_t)((l5[(v >> 4) & 3u] << 11) | (l6[(v >> 2) & 3u] << 5) | l5[v & 3u]);
}
#endif
