/* fw2/cores/mpreset_core.h -- meter presets as separate files (docs/features/METER_PRESETS_EXTERNAL.md, B-966). Pure and host-tested (sim/test_p2_mpreset.py against the Python writer tools/player2/mpreset.py): every byte
 * comes from a buffer the caller read (the firmware reads one small file at a time from the card, like a cover), nothing is allocated and no read goes outside the buffer.
 *
 * Two kinds of file per meter, under <media root>/tau-presets/<meter id>/ (the manifest's `index`, e.g. 12; NOT the meter key; written by Omega; the factory set by tools/player2/mpreset.py):
 *   index.tpi  "TPRI" | version u8 = 1 | reserved u8 = 0 | meter id u16 | crc32 u32 (of everything after these 12 bytes) | count u16 | count x { id u8 | flags u8 (bit 0: hidden) | name[16] NUL padded }
 *              the presets of the meter in list order (up to 127, ids 0..126; the id is what is remembered, never the position);
 *   p<id>.tpr  "TPRS" | version u8 = 1 | id u8 | meter id u16 | crc32 u32 (of everything after these 12 bytes) | nkeys u8 | name_len u8 (0..15) | name | nkeys x { key u16 | value u16 }   (little endian)
 * A preset is SPARSE and keyed by parameter name: `key` is a 16-bit hash of the manifest's parameter key (mp_key_hash, the same function in tools/player2/mpreset.py). A parameter the preset does not mention takes the
 * meter's default, a key the meter does not have is ignored, a value is clamped to the parameter's range: a preset survives a meter that gained or lost parameters. */
#ifndef MPRESET_CORE_H
#define MPRESET_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define MP_IDX_MAGIC 0x49525054u      /* "TPRI" little endian */
#define MP_PRE_MAGIC 0x53525054u      /* "TPRS" */
#define MP_VERSION 1u
#define MP_NAME 16u
#define MP_IDX_ENTRY (2u + MP_NAME)
#define MP_ID_MAX 126u                                        /* the largest preset id: the choice is persisted in 7 bits as id + 1 (SF_MPID*, B-968) */
#define MP_IDX_MAX 127u                                       /* presets per meter: ids 0..126 (14 + 127 x 18 = 2,300 bytes, one read into the 4,096-byte tag buffer) */
#define MP_IDX_MAX_BYTES (14u + MP_IDX_MAX * MP_IDX_ENTRY)
#define MP_PRE_MAX_BYTES (14u + 15u + 4u * 64u)
enum { MP_OK = 0, MP_E_SIZE = 1, MP_E_MAGIC = 2, MP_E_VERSION = 3, MP_E_CRC = 4, MP_E_METER = 5, MP_E_ID = 6, MP_E_LAYOUT = 7 };
typedef struct { uint16_t key, min, max, def; } mp_param_t;     /* one parameter of the meter, from its descriptor: the key hash, the range and the default */
static inline uint32_t mp_ld16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t mp_ld32(const uint8_t *p) { return mp_ld16(p) | (mp_ld16(p + 2) << 16); }
static inline uint32_t mp_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
/* The key hash: FNV-1a over the key's bytes, the 32 bits folded to 16. */
static inline uint16_t mp_key_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return (uint16_t)((h ^ (h >> 16)) & 0xFFFFu);
}
/* ---- the index ---- */
static inline int mp_index_check(const uint8_t *d, uint32_t n, uint32_t meter_id)
{
    if (n < 14u || n > MP_IDX_MAX_BYTES) return MP_E_SIZE;
    if (mp_ld32(d) != MP_IDX_MAGIC) return MP_E_MAGIC;
    if (d[4] != MP_VERSION) return MP_E_VERSION;
    if (mp_crc(d + 12, n - 12u) != mp_ld32(d + 8)) return MP_E_CRC;
    if (mp_ld16(d + 6) != meter_id) return MP_E_METER;
    if (d[5] != 0u || mp_ld16(d + 12) > MP_IDX_MAX || n != 14u + mp_ld16(d + 12) * MP_IDX_ENTRY) return MP_E_LAYOUT;      /* byte 5 is reserved and must be 0: it is outside the CRC */
    for (uint32_t i = 0; i < mp_ld16(d + 12); i++) if (d[14u + i * MP_IDX_ENTRY] > MP_ID_MAX) return MP_E_ID;               /* an id the persisted field cannot hold */
    return MP_OK;
}
static inline uint32_t mp_index_count(const uint8_t *d) { return mp_ld16(d + 12); }
/* Entry i (0 = first in the list): id, flags and the name (NUL terminated, at most 15 characters). Valid only after mp_index_check() returned 0. */
static inline void mp_index_entry(const uint8_t *d, uint32_t i, uint8_t *id, uint8_t *flags, char *name)
{
    const uint8_t *e = d + 14u + i * MP_IDX_ENTRY;
    *id = e[0]; *flags = e[1];
    uint32_t k = 0; for (; k < MP_NAME - 1u && e[2u + k]; k++) name[k] = (char)e[2u + k];
    name[k] = 0;
}
/* The position of preset `id` in the list, or -1: what is remembered is the id, the list may have been reordered or replaced since. */
static inline int mp_index_find(const uint8_t *d, uint32_t id)
{
    const uint32_t n = mp_index_count(d);
    for (uint32_t i = 0; i < n; i++) if (d[14u + i * MP_IDX_ENTRY] == id) return (int)i;
    return -1;
}
/* ---- a preset ---- */
static inline int mp_preset_check(const uint8_t *d, uint32_t n, uint32_t meter_id, uint32_t want_id)
{
    if (n < 14u || n > MP_PRE_MAX_BYTES) return MP_E_SIZE;
    if (mp_ld32(d) != MP_PRE_MAGIC) return MP_E_MAGIC;
    if (d[4] != MP_VERSION) return MP_E_VERSION;
    if (mp_crc(d + 12, n - 12u) != mp_ld32(d + 8)) return MP_E_CRC;
    if (mp_ld16(d + 6) != meter_id) return MP_E_METER;
    if (d[5] != want_id || d[5] > MP_ID_MAX) return MP_E_ID;
    if (d[13] > 15u || n != 14u + d[13] + 4u * (uint32_t)d[12]) return MP_E_LAYOUT;
    return MP_OK;
}
static inline void mp_preset_name(const uint8_t *d, char *name) { uint32_t k = 0; for (; k < d[13]; k++) name[k] = (char)d[14u + k]; name[k] = 0; }
/* Every parameter to its default (what a meter runs on when no preset is loaded, and the start of every preset). */
static inline void mp_defaults(const mp_param_t *ps, uint32_t np, uint16_t *vals)
{
    for (uint32_t i = 0; i < np; i++) vals[i] = ps[i].def;
}
/* Applies a checked preset: defaults first, then each key the meter has, its value clamped to the range. Returns the number of keys that matched a parameter. */
static inline uint32_t mp_preset_apply(const uint8_t *d, const mp_param_t *ps, uint32_t np, uint16_t *vals)
{
    mp_defaults(ps, np, vals);
    uint32_t hit = 0;
    const uint8_t *e = d + 14u + d[13];
    for (uint32_t k = 0; k < d[12]; k++, e += 4) {
        const uint32_t key = mp_ld16(e), v = mp_ld16(e + 2);
        for (uint32_t i = 0; i < np; i++) {
            if (ps[i].key != key) continue;
            uint32_t x = v;
#if BUG != 1
            if (x < ps[i].min) x = ps[i].min;
            if (x > ps[i].max) x = ps[i].max;
#endif
            vals[i] = (uint16_t)x; hit++;
            break;
        }
    }
    return hit;
}
#endif
