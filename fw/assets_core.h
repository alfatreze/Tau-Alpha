/* tau-assets.bin reader, portable part (no MMIO, no libc). Format and writer: tools/tau_assets.py, docs/THEME_FILE_FORMAT.md.
 * Container: "TAUA" | version u16 | section_count u16 | crc32 of the section table | table of (tag[4], offset, length, crc32 of the
 * section) | sections. THEM section: "TTHM" | version u16 | role_count u8 | theme_count u8 | crc32 of the rest | themes of
 * name[16] | bg_luma dark, light | 2 pad | dark[role_count] u16 | light[role_count] u16 (all little endian).
 * Rules (same spirit as the library and cold image): every CRC and the version are checked before anything is used; any failure
 * means built-in themes only, reported as a code on the Info page. Values are clamped, the file's own claims never are trusted. */
#ifndef TAU_ASSETS_CORE_H
#define TAU_ASSETS_CORE_H

#include <stdint.h>
#include "library_core.h"      /* lib_crc_update, lib_ld16/32 */

enum { AS_OK = 0, AS_E_READ = 30, AS_E_MAGIC = 31, AS_E_VERSION = 32, AS_E_SIZE = 33, AS_E_CRC = 34, AS_E_NOTHEME = 35 };
#define AS_MAX_SECTIONS 8u

static uint32_t as_crc(const uint8_t *p, uint32_t n) { return LIB_CRC_DONE(lib_crc_update(LIB_CRC_INIT, p, n)); }

/* Bytes the whole file must have, from its first 12 + 16*sections bytes (the header and table); 0 = not a valid header. */
static uint32_t as_total_size(const uint8_t *b, uint32_t have)
{
    if (have < 12u || b[0] != 'T' || b[1] != 'A' || b[2] != 'U' || b[3] != 'A') return 0u;
    uint32_t n = lib_ld16(b + 6), end = 12u + 16u * n;
    if (n > AS_MAX_SECTIONS || have < end) return 0u;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = b + 12u + 16u * i;
        uint32_t off = lib_ld32(e + 4), len = lib_ld32(e + 8);
        if (off > 0xFFFFu || len > 0xFFFFu) return 0u;
        if (off + len > end) end = off + len;
    }
    return end;
}

/* Finds section `tag` in a whole file of `len` bytes; checks version, table CRC and that section's CRC. */
static int as_find(const uint8_t *b, uint32_t len, const char *tag, uint32_t *off, uint32_t *n)
{
    if (len < 12u || b[0] != 'T' || b[1] != 'A' || b[2] != 'U' || b[3] != 'A') return AS_E_MAGIC;
    if (lib_ld16(b + 4) != 1u) return AS_E_VERSION;
    uint32_t cnt = lib_ld16(b + 6);
    if (cnt > AS_MAX_SECTIONS || len < 12u + 16u * cnt) return AS_E_SIZE;
    if (as_crc(b + 12, 16u * cnt) != lib_ld32(b + 8)) return AS_E_CRC;
    for (uint32_t i = 0; i < cnt; i++) {
        const uint8_t *e = b + 12u + 16u * i;
        if (e[0] != (uint8_t)tag[0] || e[1] != (uint8_t)tag[1] || e[2] != (uint8_t)tag[2] || e[3] != (uint8_t)tag[3]) continue;
        uint32_t o = lib_ld32(e + 4), l = lib_ld32(e + 8);
        if (o > len || l > len - o) return AS_E_SIZE;
        if (as_crc(b + o, l) != lib_ld32(e + 12)) return AS_E_CRC;
        *off = o; *n = l;
        return AS_OK;
    }
    return AS_E_NOTHEME;
}

/* One parsed theme, filled by as_themes(). `roles` is flat: theme i, polarity p, role r at (i*2+p)*stride + r, `cap` of them used; roles the file does not carry (a shorter
 * role_count) keep whatever the caller pre-filled, roles beyond `cap` are ignored. */
typedef struct { char name[16]; uint8_t luma[2]; } as_theme_t;

/* d: the THEM section. Returns AS_OK and the number of themes stored (at most `max`), or a code. */
static int as_themes(const uint8_t *d, uint32_t n, uint32_t cap, uint32_t max, as_theme_t *meta, uint16_t *roles, uint32_t stride, uint32_t *count)
{
    if (n < 12u || d[0] != 'T' || d[1] != 'T' || d[2] != 'H' || d[3] != 'M') return AS_E_MAGIC;
    uint32_t rc = d[6], tc = d[7];
    if (lib_ld16(d + 4) != 1u || !rc || !tc) return AS_E_VERSION;
    uint32_t per = 20u + 4u * rc;
    if (n != 12u + per * tc) return AS_E_SIZE;
    if (as_crc(d + 12, n - 12u) != lib_ld32(d + 8)) return AS_E_CRC;
    if (cap > stride) cap = stride;
    uint32_t k = tc < max ? tc : max;
    for (uint32_t i = 0; i < k; i++) {
        const uint8_t *e = d + 12u + per * i;
        uint32_t j = 0;
        for (; j < 15u && e[j]; j++) meta[i].name[j] = (char)e[j];
        for (; j < 16u; j++) meta[i].name[j] = 0;
        for (uint32_t p = 0; p < 2u; p++) {
            uint32_t l = e[16u + p];
            meta[i].luma[p] = (uint8_t)(l < 20u ? 20u : (l > 235u ? 235u : l));
            for (uint32_t r = 0; r < rc && r < cap; r++) roles[(i * 2u + p) * stride + r] = (uint16_t)lib_ld16(e + 20u + (p * rc + r) * 2u);
        }
    }
    *count = k;
    return AS_OK;
}
#endif
