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
#include "meter_module.h"       /* mtr_data_t: METR presets are validated against the compiled parameter tables */

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

/* ---- METR: per-meter preset sets (tools/tau_assets.py pack_meters, docs/THEME_FILE_FORMAT.md) ---------------------------------------
 * "TMTR" | version u16 | 2 pad | crc32 of the rest | count u16 | count x { len u16 | id u8 | schema u8 | flags u8 | order u8 | npre u8 | nparams u8 |
 * npre x { name[16] | nparams values, u16 parameters two bytes } | default_preset u8 }.
 * Rules: a meter the firmware does not know, a newer schema, a different parameter count, a size that does not add up or a preset count outside
 * 1..maxpre skips THAT meter only. Every value is clamped to the compiled table, enum/bool bounds included; names are reduced to printable
 * upper-case ASCII. A meter is committed only when all its presets parsed. The file replaces a meter's presets and picks its boot preset; it
 * cannot add a meter. `flags` and `order` are read but not applied yet (reordering and hiding meters is not built). */
static int as_metr(const uint8_t *d, uint32_t n, mtr_data_t *const *mods, uint32_t nmods, uint32_t *applied)
{
    if (n < 14u || d[0] != 'T' || d[1] != 'M' || d[2] != 'T' || d[3] != 'R') return AS_E_MAGIC;
    if (lib_ld16(d + 4) != 1u) return AS_E_VERSION;
    if (as_crc(d + 12, n - 12u) != lib_ld32(d + 8)) return AS_E_CRC;
    uint32_t count = lib_ld16(d + 12), pos = 14u, done = 0;
    for (uint32_t k = 0; k < count; k++) {
        if (pos + 2u > n) return AS_E_SIZE;
        uint32_t len = lib_ld16(d + pos), at = pos + 2u;
        if (len < 7u || at + len > n) return AS_E_SIZE;
        pos = at + len;
        const uint8_t *e = d + at;
        const uint32_t id = e[0], schema = e[1], npre = e[4], nparams = e[5];
        mtr_data_t *md = 0;
        for (uint32_t i = 0; i < nmods; i++) if (mods[i]->viz == id) md = mods[i];
        if (!md || schema > 1u || nparams != md->n || npre < 1u || npre > md->maxpre) continue;
        uint32_t psize = 16u;
        for (uint32_t i = 0; i < md->n; i++) psize += (md->p[i].type == MTR_U16) ? 2u : 1u;
        if (len != 6u + npre * psize + 1u || e[6u + npre * psize] >= npre) continue;
        uint16_t vals[MTR_MAX_PRESET_VALUES];
        char names[8][16];
        if (md->n > MTR_MAX_PARAMS || npre > 8u) continue;
        uint32_t q = 6u;
        for (uint32_t pr = 0; pr < npre; pr++) {
            uint32_t j = 0;
            for (uint32_t c = 0; c < 15u; c++) {
                uint8_t ch = e[q + c];
                if (!ch) break;
                if (ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 32u);
                names[pr][j++] = (ch >= 32u && ch < 127u) ? (char)ch : '?';
            }
            names[pr][j] = 0;
            q += 16u;
            for (uint32_t i = 0; i < md->n; i++) {
                const mtr_param_t *p = &md->p[i];
                uint32_t v = e[q++];
                if (p->type == MTR_U16) v |= (uint32_t)e[q++] << 8;
                if (v < p->min) v = p->min; else if (v > p->max) v = p->max;
                vals[pr * MTR_MAX_PARAMS + i] = (uint16_t)v;
            }
        }
        for (uint32_t pr = 0; pr < npre; pr++) {                   /* commit: every preset parsed */
            for (uint32_t i = 0; i < md->n; i++) md->pre[pr * md->n + i] = vals[pr * MTR_MAX_PARAMS + i];
            for (uint32_t c = 0; c < 16u; c++) md->pre_buf[pr][c] = names[pr][c];
            md->pre_names[pr] = md->pre_buf[pr];
        }
        md->npre = (uint8_t)npre;
        mtr_apply_preset(md, e[6u + npre * psize]);
        done++;
    }
    *applied = done;
    return AS_OK;
}
#endif
