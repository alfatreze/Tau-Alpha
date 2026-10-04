/* Pixel-grid fidelity test patterns (barcode study, docs/features/BARCODE_STUDY.md section 4). Pure and portable: the Diagnostics
 * page (fw/suite.inc, PIXEL GRID TEST) draws these through the SDRAM mailbox and tools/pixgrid_check.py regenerates the same values from
 * a card screenshot. Every pattern is a function of (x, y) on the 400x360 screen, one RGB565 value per pixel. */
#ifndef PIXGRID_H
#define PIXGRID_H
#include <stdint.h>

#define PG_W 400u
#define PG_H 360u
#define PG_PATTERNS 4u

static inline uint16_t pg_pixel(uint32_t pat, uint32_t x, uint32_t y)
{
    const uint32_t idx = y * PG_W + x;
    switch (pat) {
    case 0:  return (uint16_t)((idx * 0x9E3779B1u) >> 16);                    /* dense pseudo-random: every pixel an independent 16-bit value */
    case 1:  return ((x ^ y) & 1u) ? 0xFFFFu : 0x0000u;                       /* 1 px checkerboard: worst case for any scaling or filtering */
    case 2: {                                                                 /* smooth colour field: filtering shows as a changed gradient */
        uint32_t r = x * 31u / (PG_W - 1u), g = y * 63u / (PG_H - 1u), b = (x + y) * 31u / (PG_W + PG_H - 2u);
        return (uint16_t)((r << 11) | (g << 5) | b);
    }
    default: return (uint16_t)(idx & 0xFFFFu);                                /* counter: neighbouring pixels differ in one low bit */
    }
}
#endif
