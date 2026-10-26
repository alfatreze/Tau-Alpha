/* fw/halcyon_prst.h -- the PRST section reader (B-641): Halcyon user presets from tau-assets.bin. Portable (no MMIO, no libc); format and writer: tools/halcyon_assets.py,
 * docs/features/HALCYON_DATA_FORMAT.md. Section: "TPRS" | version u16 | 0 u8 | count u8 | crc32 of everything after the 12-byte header, then `count` entries
 *   type u8 | name[16] | flags u8 | payload     type 0 (control): 6 x int8 warmth, bass, vocal, punch, sibilance, air
 *                                                type 1 (raw): nstages u8 (1..10) | preamp int24 | nstages x 5 x int24 (b0 b1 b2 a1 a2, Q2.22 little endian)
 * Same rules as the other sections: the version, the CRC and every size are checked before anything is used, and ANY failure means no user presets at all (a half-valid file is
 * not half-loaded). Beyond the writer's own rules the reader re-checks what the engine must never be given: control positions inside their ranges, a preamp above zero and at most
 * unity (attenuate only), a name that ends inside its 16 bytes, and **every raw stage stable at Q2.22** (the integer Jury test, exactly the writer's pole test). */
#ifndef TAU_HALCYON_PRST_H
#define TAU_HALCYON_PRST_H
#include <stdint.h>
#include "library_core.h"      /* lib_crc_update, lib_ld16/32 */
#include "halcyon_core.h"
#ifndef HP_FN
#define HP_FN static            /* the firmware defines this as COLD_FN static: the reader runs once at boot from PSRAM */
#endif

enum { HP_OK = 0, HP_E_MAGIC = 40, HP_E_VERSION, HP_E_CRC, HP_E_SIZE, HP_E_ENTRY };
#define HP_MAX_PRESETS 8u
#define HP_MAX_RAW_STAGES 10u
#define HP_ENTRY_HEAD 18u                   /* type, name[16], flags */
#define HP_Q22_ONE (1 << 22)

HP_FN int32_t hp_i24(const uint8_t *p) { int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)); return (v << 8) >> 8; }

/* Poles of 1 + a1 z^-1 + a2 z^-2 strictly inside the unit circle: |a2| < 1 and |a1| < 1 + a2 (Jury), in Q2.22 integers. */
HP_FN int hp_stable(int32_t a1, int32_t a2)
{
    if (a2 <= -HP_Q22_ONE || a2 >= HP_Q22_ONE) return 0;
    int32_t m = a1 < 0 ? -a1 : a1;
    return m < HP_Q22_ONE + a2;
}

/* Validates the whole section (d, n bytes) and stores the start offset of each entry in offs[] (up to HP_MAX_PRESETS). Returns HP_OK and *count, or a code. */
HP_FN int hp_check(const uint8_t *d, uint32_t n, uint16_t *offs, uint8_t *count)
{
    if (n < 12u || d[0] != 'T' || d[1] != 'P' || d[2] != 'R' || d[3] != 'S') return HP_E_MAGIC;
    uint32_t cnt = d[7];
    if (lib_ld16(d + 4) != 1u || d[6] != 0u || cnt < 1u || cnt > HP_MAX_PRESETS) return HP_E_VERSION;
    if (LIB_CRC_DONE(lib_crc_update(LIB_CRC_INIT, d + 12, n - 12u)) != lib_ld32(d + 8)) return HP_E_CRC;
    uint32_t pos = 12u;
    for (uint32_t k = 0; k < cnt; k++) {
        if (pos + HP_ENTRY_HEAD > n) return HP_E_SIZE;
        const uint8_t *e = d + pos;
        if (e[0] > 1u || e[16] != 0u || e[17] != 0u) return HP_E_ENTRY;          /* a name must end inside its 16 bytes; flags are 0 in version 1 */
        offs[k] = (uint16_t)pos;
        pos += HP_ENTRY_HEAD;
        if (e[0] == 0u) {
            if (pos + 6u > n) return HP_E_SIZE;
            for (uint32_t i = 0; i < 6u; i++) {
                int32_t v = (int8_t)d[pos + i];
                if (v < (i == 4u ? 0 : -5) || v > 5) return HP_E_ENTRY;
            }
            pos += 6u;
        } else {
            if (pos + 4u > n) return HP_E_SIZE;
            uint32_t ns = d[pos];
            int32_t pre = hp_i24(d + pos + 1);
            if (ns < 1u || ns > HP_MAX_RAW_STAGES || pre <= 0 || pre > HP_Q22_ONE) return HP_E_ENTRY;
            pos += 4u;
            if (pos + 15u * ns > n) return HP_E_SIZE;
            for (uint32_t s = 0; s < ns; s++)
                if (!hp_stable(hp_i24(d + pos + 15u * s + 9u), hp_i24(d + pos + 15u * s + 12u))) return HP_E_ENTRY;
            pos += 15u * ns;
        }
    }
    if (pos != n) return HP_E_SIZE;
    *count = (uint8_t)cnt;
    return HP_OK;
}

/* One validated entry (e = d + offs[k]): the name pointer, and either the six controls (returns 0) or the raw bank (returns the stage count, coef[stage*5+k] and *pre filled). */
static inline const char *hp_name(const uint8_t *e) { return (const char *)(e + 1); }
HP_FN uint32_t hp_entry(const uint8_t *e, hal_ctl_t *c, int32_t *coef, int32_t *pre)
{
    const uint8_t *p = e + HP_ENTRY_HEAD;
    if (e[0] == 0u) {
        c->warmth = (int8_t)p[0]; c->bass = (int8_t)p[1]; c->vocal = (int8_t)p[2]; c->punch = (int8_t)p[3]; c->sibilance = (int8_t)p[4]; c->air = (int8_t)p[5];
        return 0u;
    }
    const uint32_t ns = p[0];
    *pre = hp_i24(p + 1);
    for (uint32_t i = 0; i < ns * 5u; i++) coef[i] = hp_i24(p + 4u + 3u * i);
    return ns;
}
#endif
