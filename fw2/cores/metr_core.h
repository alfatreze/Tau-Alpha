/* fw2/cores/metr_core.h -- the METR section of tau-assets.bin (docs/THEME_FILE_FORMAT.md; written by tools/tau_assets.py): per-meter preset sets that REPLACE a meter's built-in presets. Pure, host-tested
 * (sim/test_p2_metr.py reads files the Python writer produced). Layout: "TMTR" | version u16 = 1 | 2 pad | crc32 of the rest | count u16 | count x { len u16 | id u8 | schema u8 | flags u8 | order u8 | npre u8 |
 * nparams u8 | npre x { name[16] | values } | default u8 }; a value is one byte, two (little endian) for a number whose range goes above 255. A meter whose entry does not fit (unknown id, newer schema, a
 * different parameter count, a size that does not add up, 0 or more than 8 presets, a default outside the set) is skipped alone; every value is clamped to the compiled range. */
#ifndef METR_CORE_H
#define METR_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define METR_MAX_PRE 8u
#define METR_NAME 16u
typedef struct { uint16_t min, max; uint8_t wide, enum_n, kind; } metr_p_t;           /* what a value needs: kind 0 number, 1 choice (enum_n values), 2 on/off */
typedef struct { uint32_t off; uint8_t npre, def, psize; } metr_e_t;                      /* where an entry's first preset starts (offset of its name), its preset count, boot preset and bytes per preset */
static inline uint32_t metr_ld16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t metr_ld32(const uint8_t *p) { return metr_ld16(p) | (metr_ld16(p + 2) << 16); }
static inline uint32_t metr_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
/* 0 = the section is valid. */
static inline int metr_check(const uint8_t *d, uint32_t n)
{
    if (n < 14u || d[0] != 'T' || d[1] != 'M' || d[2] != 'T' || d[3] != 'R') return 1;
    if (metr_ld16(d + 4) != 1u) return 2;
    if (metr_crc(d + 12, n - 12u) != metr_ld32(d + 8)) return 3;
    return 0;
}
/* Finds meter `id`'s entry for a meter with `np` parameters described by `ps`. Returns 1 and fills `out`, 0 when there is none or it does not fit. */
static inline int metr_find(const uint8_t *d, uint32_t n, uint32_t id, const metr_p_t *ps, uint32_t np, metr_e_t *out)
{
    uint32_t count = metr_ld16(d + 12), pos = 14u;
    for (uint32_t k = 0; k < count; k++) {
        if (pos + 2u > n) return 0;
        const uint32_t len = metr_ld16(d + pos), at = pos + 2u;
        if (len < 7u || at + len > n) return 0;
        pos = at + len;
        const uint8_t *e = d + at;
        if (e[0] != id) continue;
        const uint32_t schema = e[1], npre = e[4], nparams = e[5];
        if (schema > 1u || nparams != np || npre < 1u || npre > METR_MAX_PRE) return 0;
        uint32_t psize = METR_NAME;
        for (uint32_t i = 0; i < np; i++) psize += ps[i].wide ? 2u : 1u;
        if (len != 6u + npre * psize + 1u || e[6u + npre * psize] >= npre) return 0;
        out->off = at + 6u; out->npre = (uint8_t)npre; out->def = e[6u + npre * psize]; out->psize = (uint8_t)psize;
        return 1;
    }
    return 0;
}
/* The name of preset p: printable upper-case ASCII, at most 15 characters. */
static inline void metr_name(const uint8_t *d, const metr_e_t *e, uint32_t p, char *out)
{
    const uint8_t *s = d + e->off + p * e->psize; uint32_t j = 0;
    for (uint32_t c = 0; c < METR_NAME - 1u; c++) {
        uint8_t ch = s[c]; if (!ch) break;
        if (ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 32u);
        out[j++] = (ch >= 32u && ch < 127u) ? (char)ch : '?';
    }
    out[j] = 0;
}
/* The values of preset p into vals[np], each clamped to its range. */
static inline void metr_values(const uint8_t *d, const metr_e_t *e, uint32_t p, const metr_p_t *ps, uint32_t np, uint16_t *vals)
{
    const uint8_t *s = d + e->off + p * e->psize + METR_NAME;
    for (uint32_t i = 0; i < np; i++) {
        uint32_t v = *s++; if (ps[i].wide) v |= (uint32_t)(*s++) << 8;
        uint32_t lo = ps[i].min, hi = ps[i].max;
#if BUG == 1
        (void)lo; (void)hi;                                                                  /* mutant: no clamp */
#else
        if (v < lo) v = lo; else if (v > hi) v = hi;
#endif
        vals[i] = (uint16_t)v;
    }
}
#endif
