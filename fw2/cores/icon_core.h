/* fw2/cores/icon_core.h -- the player icons (TICN record, docs/features/FONT_SPEC.md 13): finding an icon in the record, walking its rows as runs of equal coverage, and the 16-entry colour table that turns 4-bit
 * coverage into real colours between a background and a foreground. Pure, host-tested (sim/test_p2_icon.py against the engine model). */
#ifndef ICON_CORE_H
#define ICON_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint8_t w, h; const uint8_t *data; uint32_t plane_x; } ti_icon_t;
static inline uint32_t ti_ld16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t ti_ld32(const uint8_t *p) { return ti_ld16(p) | (ti_ld16(p + 2) << 16); }
static inline uint32_t ti_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
/* 0 = a record this firmware can use (magic, version, CRC, every icon's data length, exact size); *count = how many icons. */
static inline int ti_check(const uint8_t *rec, uint32_t len, uint32_t *count)
{
    if (len < 16u || rec[0] != 'T' || rec[1] != 'I' || rec[2] != 'C' || rec[3] != 'N' || ti_ld16(rec + 4) != 1u) return 1;
#if BUG != 1
    if (ti_crc(rec + 12, len - 12u) != ti_ld32(rec + 8)) return 1;
#endif
    uint32_t n = rec[12], off = 16u;
    for (uint32_t i = 0; i < n; i++) {
        if (off + 18u > len) return 1;
        const uint32_t w = rec[off + 1], h = rec[off + 2], dl = ti_ld16(rec + off + 16);
        if (dl != h * ((w + 1u) / 2u)) return 1;
        off += 18u + dl;
    }
    if (off != len) return 1;
    *count = n;
    return 0;
}
/* Icon `id`: its size, its data, and the x of its plane in the stash (the planes sit side by side, in id order, from x = 0). 0 = found. */
static inline int ti_find(const uint8_t *rec, uint32_t len, uint32_t id, ti_icon_t *o)
{
    uint32_t off = 16u, px = 0;
    for (uint32_t i = 0; off + 18u <= len && i < rec[12]; i++) {
        const uint32_t w = rec[off + 1], dl = ti_ld16(rec + off + 16);
        if (rec[off] == id) { o->w = (uint8_t)w; o->h = rec[off + 2]; o->data = rec + off + 18u; o->plane_x = px; return 0; }
        off += 18u + dl; px += w;
    }
    return 1;
}
static inline uint32_t ti_cov(const ti_icon_t *ic, uint32_t x, uint32_t y) { const uint8_t b = ic->data[y * ((ic->w + 1u) / 2u) + x / 2u]; return (x & 1u) ? (uint32_t)(b >> 4) : (uint32_t)(b & 15u); }
/* The next run of equal coverage in row y starting at x: returns its length, *cov its value. */
static inline uint32_t ti_run(const ti_icon_t *ic, uint32_t x, uint32_t y, uint32_t *cov)
{
    const uint32_t c = ti_cov(ic, x, y); uint32_t n = 1;
    while (x + n < ic->w && ti_cov(ic, x + n, y) == c) n++;
    *cov = c; return n;
}
/* Colour table entry c (0..15): the background blended toward the foreground by c / 15, per RGB565 channel, rounded. */
static inline uint16_t ti_colour(uint16_t bg, uint16_t fg, uint32_t c)
{
#if BUG == 2
    c = c > 7u ? 15u : 0u;                                           /* mutant: the anti-aliasing steps collapse to two colours */
#endif
    const uint32_t br = bg >> 11, bgc = (bg >> 5) & 63u, bb = bg & 31u, fr = fg >> 11, fgc = (fg >> 5) & 63u, fb = fg & 31u;
    const uint32_t r = (br * (15u - c) + fr * c + 7u) / 15u, g = (bgc * (15u - c) + fgc * c + 7u) / 15u, b = (bb * (15u - c) + fb * c + 7u) / 15u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}
#endif
