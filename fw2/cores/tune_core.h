/* fw2/cores/tune_core.h -- saving a tuned meter (the Configure page's edits). The persisted words 8..11 and 16, 17, 18, 21 (interact.json ids 20..23 and 27, 28, 29, 33; 31 bits each = 124 bits per slot) hold two slots, each ONE
 * tuning: which meter, which preset it started from and the parameters that differ from that preset, so a tuning survives a restart. persist_layout 2 (external meters, docs/features/PERSISTENCE_LAYOUT.md): the meter is no longer a table
 * index but the KEY of the meter's identity, so a tuning follows its meter when meters are installed, removed or reordered and is refused when the meter's parameters changed. Layout, least significant bit first, 31 bits of each word in order:
 *   check 8 | key 20 | preset 7 | count 4 | count x { index 6 | value, as many bits as the parameter's largest value needs }          the entries start at bit 39: 85 bits for them (layout 1 had 100)
 * key = the low 16 bits of the meter's id_hash, then the low 4 bits of its params_rev (tune_key); `preset` is the anchor: the preset FILE's id + 1 when the meter runs on preset files, else the position in the pack's list.
 * `check` folds the bytes after it with its own seed (0x3C; layout 1 used 0x5A, so a record of either layout is refused by the reader of the other, and a migration can tell them apart). Parameters are taken in index order until the bits run
 * out (the rest go back to the preset after a restart). The record of layout 1 (check 8 | meter 5 | preset 7 | count 4 | entries from bit 24) is read only by the migration (tune_v1_unpack, tune_convert_v1).
 * Pure, host-tested (sim/test_p2_tune.py against an independent Python implementation of this layout). */
#ifndef TUNE_CORE_H
#define TUNE_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define TUNE_WORDS 4u
#define TUNE_BITS (TUNE_WORDS * 31u)
#define TUNE_MAX 24u
static inline uint32_t tune_nbits(uint32_t max) { uint32_t n = 1; while (n < 16u && (max >> n)) n++; return n; }
static inline void tune_put(uint8_t *b, uint32_t *pos, uint32_t v, uint32_t n) { for (uint32_t i = 0; i < n; i++, (*pos)++) { if ((v >> i) & 1u) b[*pos >> 3] |= (uint8_t)(1u << (*pos & 7u)); } }
static inline uint32_t tune_get(const uint8_t *b, uint32_t *pos, uint32_t n) { uint32_t v = 0; for (uint32_t i = 0; i < n; i++, (*pos)++) v |= (uint32_t)((b[*pos >> 3] >> (*pos & 7u)) & 1u) << i; return v; }
#define TUNE_HEAD 39u                  /* the first bit of the entries */
#define TUNE_HEAD_V1 24u
#define TUNE_SEED 0x3Cu
#define TUNE_SEED_V1 0x5Au
#define TUNE_KEY_NONE 0xFFFFFFFFu      /* a key that no record carries (20 bits at most): "no such meter" */
static inline uint8_t tune_check_seed(const uint8_t *b, uint8_t seed)
{
    uint8_t c = seed;
    for (uint32_t i = 1; i < 16u; i++) { c = (uint8_t)((c << 1) | (c >> 7)); c = (uint8_t)(c + b[i]); }
    return c;
}
static inline uint8_t tune_check(const uint8_t *b)
{
    uint8_t c = tune_check_seed(b, TUNE_SEED);
#if BUG == 1
    c = TUNE_SEED;                                                                          /* mutant: the check ignores the data */
#endif
    return c;
}
static inline uint32_t tune_key(uint32_t id_hash, uint32_t params_rev) { return (id_hash & 0xFFFFu) | ((params_rev & 15u) << 16); }
static inline void tune_to_words(const uint8_t *b, uint32_t *w) { for (uint32_t k = 0; k < TUNE_WORDS; k++) { uint32_t pos = k * 31u; w[k] = tune_get(b, &pos, 31u); } }
static inline void tune_from_words(const uint32_t *w, uint8_t *b) { for (uint32_t i = 0; i < 16u; i++) b[i] = 0; for (uint32_t k = 0; k < TUNE_WORDS; k++) { uint32_t pos = k * 31u; tune_put(b, &pos, w[k] & 0x7FFFFFFFu, 31u); } }
/* Stores the n_in entries (idx[], val[], in index order; `max` the largest value of each parameter) of the meter `key` that started from `preset`. Entries that do not fit are skipped and at most 15 are stored. Returns how many were
 * stored; the words are all zero when none. */
static inline uint32_t tune_build(uint32_t key, uint32_t preset, const uint8_t *idx, const uint16_t *val, uint32_t n_in, const uint16_t *max, uint32_t *w)
{
    uint8_t b[16]; for (uint32_t i = 0; i < 16u; i++) b[i] = 0;
    uint32_t pos = TUNE_HEAD, n = 0, cnt_pos = 35u;
    for (uint32_t k = 0; k < n_in && n < 15u; k++) {
        const uint32_t nb = tune_nbits(max[idx[k]]);
        if (pos + 6u + nb > TUNE_BITS) continue;
        tune_put(b, &pos, idx[k], 6u); tune_put(b, &pos, val[k], nb); n++;
    }
    if (!n) { for (uint32_t k = 0; k < TUNE_WORDS; k++) w[k] = 0; return 0; }
    uint32_t hp = 8u; tune_put(b, &hp, key & 0xFFFFFu, 20u); tune_put(b, &hp, preset & 127u, 7u); tune_put(b, &cnt_pos, n, 4u);
    uint32_t cp = 0; tune_put(b, &cp, tune_check(b), 8u);
    tune_to_words(b, w);
    return n;
}
/* Packs the parameters of `mv` that differ from `base` (np of them, `max` the largest value of each). `meter` is the 20-bit KEY (tune_key) of the meter; a caller that still passes a table index stores that number as the key. */
static inline uint32_t tune_pack(uint32_t meter, uint32_t preset, const uint16_t *mv, const uint16_t *base, const uint16_t *max, uint32_t np, uint32_t *w)
{
    uint8_t idx[63]; uint16_t val[63]; uint32_t n = 0;
    for (uint32_t i = 0; i < np && i < 63u; i++) if (mv[i] != base[i]) { idx[n] = (uint8_t)i; val[n] = mv[i]; n++; }
    return tune_build(meter, preset, idx, val, n, max, w);
}
/* Reads a tuning. Returns the number of entries (0 = none or invalid); idx[]/val[] hold them; every index must be below np and every value within `max`. *meter is the 20-bit KEY; max_of resolves a key to the table of
 * the meter it identifies (0 for none: the tuning is refused). */
static inline uint32_t tune_unpack(const uint32_t *w, const uint16_t *(*max_of)(uint32_t meter, uint32_t *np), uint32_t *meter, uint32_t *preset, uint8_t *idx, uint16_t *val)
{
    uint8_t b[16]; tune_from_words(w, b);
    uint32_t pos = 0; const uint32_t chk = tune_get(b, &pos, 8u); *meter = tune_get(b, &pos, 20u); *preset = tune_get(b, &pos, 7u); const uint32_t n = tune_get(b, &pos, 4u);
    if (!n || chk != tune_check(b)) return 0;
    uint32_t np = 0; const uint16_t *max = max_of(*meter, &np);
    if (!max) return 0;
    for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = tune_get(b, &pos, 6u);
        if (i >= np) return 0;
        const uint32_t nb = tune_nbits(max[i]); if (pos + nb > TUNE_BITS) return 0;
        const uint32_t v = tune_get(b, &pos, nb);
        if (v > max[i]) return 0;
        idx[k] = (uint8_t)i; val[k] = (uint16_t)v;
    }
    return n;
}
/* ---- the record of layout 1 (read only by the migration) ---- */
/* a record of layout 2: its check byte matches and a count is stored (the entries are validated against the meter only when the tuning is applied) */
static inline int tune_is_v2(const uint32_t *w)
{
    uint8_t b[16]; tune_from_words(w, b);
    uint32_t pos = 0; const uint32_t chk = tune_get(b, &pos, 8u); pos = 35u; const uint32_t n = tune_get(b, &pos, 4u);
    return n && chk == tune_check_seed(b, TUNE_SEED);
}
/* the same as tune_unpack for a record of layout 1: *meter is the 5-bit table index, max_of takes the index */
static inline uint32_t tune_v1_unpack(const uint32_t *w, const uint16_t *(*max_of)(uint32_t meter, uint32_t *np), uint32_t *meter, uint32_t *preset, uint8_t *idx, uint16_t *val)
{
    uint8_t b[16]; tune_from_words(w, b);
    uint32_t pos = 0; const uint32_t chk = tune_get(b, &pos, 8u); *meter = tune_get(b, &pos, 5u); *preset = tune_get(b, &pos, 7u); const uint32_t n = tune_get(b, &pos, 4u);
    if (!n || chk != tune_check_seed(b, TUNE_SEED_V1) || !max_of) return 0;
    uint32_t np = 0; const uint16_t *max = max_of(*meter, &np);
    if (!max) return 0;
    for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = tune_get(b, &pos, 6u);
        if (i >= np) return 0;
        const uint32_t nb = tune_nbits(max[i]); if (pos + nb > TUNE_BITS) return 0;
        const uint32_t v = tune_get(b, &pos, nb);
        if (v > max[i]) return 0;
        idx[k] = (uint8_t)i; val[k] = (uint16_t)v;
    }
    return n;
}
/* One slot of layout 1 -> layout 2: a record already of layout 2 is kept; a record of layout 1 whose meter has a table (max_of) and a key (key_of, TUNE_KEY_NONE = not a shipped meter) is re-keyed with its preset anchor and its entries
 * (those that no longer fit the shorter header are dropped); anything else becomes empty. Whether the preset still exists is checked when the tuning is applied (its anchor must equal the preset's). */
static inline void tune_convert_v1(const uint32_t *old, const uint16_t *(*max_of)(uint32_t meter, uint32_t *np), uint32_t (*key_of)(uint32_t meter), uint32_t *out)
{
    uint32_t meter = 0, preset = 0; uint8_t idx[TUNE_MAX]; uint16_t val[TUNE_MAX];
    if (tune_is_v2(old)) { for (uint32_t k = 0; k < TUNE_WORDS; k++) out[k] = old[k]; return; }
    const uint32_t n = tune_v1_unpack(old, max_of, &meter, &preset, idx, val);
    uint32_t np = 0; const uint32_t key = n ? key_of(meter) : TUNE_KEY_NONE;
    if (!n || key == TUNE_KEY_NONE || !max_of(meter, &np)) { for (uint32_t k = 0; k < TUNE_WORDS; k++) out[k] = 0; return; }
    tune_build(key, preset, idx, val, n, max_of(meter, &np), out);
}
#endif
