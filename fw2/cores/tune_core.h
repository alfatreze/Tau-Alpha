/* fw2/cores/tune_core.h -- saving a tuned meter (the Configure page's edits). The persisted words 8..11 (interact.json ids 20..23, 31 bits each = 124 bits; the old Check summary used them, Player 2 does not) hold ONE
 * tuning: which meter, which preset it started from and the parameters that differ from that preset, so a tuning survives a restart. Layout, least significant bit first, 31 bits of each word in order:
 *   check 8 | meter 5 | preset 4 | count 4 | count x { index 6 | value, as many bits as the parameter's largest value needs }
 * `check` folds the bytes after it (so zeros and a stale Check summary are refused). Parameters are taken in index order until the bits run out (the rest go back to the preset after a restart).
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
static inline uint8_t tune_check(const uint8_t *b)
{
    uint8_t c = 0xA5u;
    for (uint32_t i = 1; i < 16u; i++) { c = (uint8_t)((c << 1) | (c >> 7)); c = (uint8_t)(c + b[i]); }
#if BUG == 1
    c = 0xA5u;                                                                              /* mutant: the check ignores the data */
#endif
    return c;
}
static inline void tune_to_words(const uint8_t *b, uint32_t *w) { for (uint32_t k = 0; k < TUNE_WORDS; k++) { uint32_t pos = k * 31u; w[k] = tune_get(b, &pos, 31u); } }
static inline void tune_from_words(const uint32_t *w, uint8_t *b) { for (uint32_t i = 0; i < 16u; i++) b[i] = 0; for (uint32_t k = 0; k < TUNE_WORDS; k++) { uint32_t pos = k * 31u; tune_put(b, &pos, w[k] & 0x7FFFFFFFu, 31u); } }
/* Packs the parameters of `mv` that differ from `base` (np of them, `max` the largest value of each). Returns how many were stored; the words are all zero when nothing differs. */
static inline uint32_t tune_pack(uint32_t meter, uint32_t preset, const uint16_t *mv, const uint16_t *base, const uint16_t *max, uint32_t np, uint32_t *w)
{
    uint8_t b[16]; for (uint32_t i = 0; i < 16u; i++) b[i] = 0;
    uint32_t pos = 21u, n = 0, cnt_pos = 17u;
    for (uint32_t i = 0; i < np && i < 63u && n < 15u; i++) {
        if (mv[i] == base[i]) continue;
        const uint32_t nb = tune_nbits(max[i]);
        if (pos + 6u + nb > TUNE_BITS) continue;
        tune_put(b, &pos, i, 6u); tune_put(b, &pos, mv[i], nb); n++;
    }
    if (!n) { for (uint32_t k = 0; k < TUNE_WORDS; k++) w[k] = 0; return 0; }
    uint32_t hp = 8u; tune_put(b, &hp, meter & 31u, 5u); tune_put(b, &hp, preset & 15u, 4u); tune_put(b, &cnt_pos, n, 4u);
    uint32_t cp = 0; tune_put(b, &cp, tune_check(b), 8u);
    tune_to_words(b, w);
    return n;
}
/* Reads a tuning. Returns the number of entries (0 = none or invalid); idx[]/val[] hold them; every index must be below np and every value within `max`. */
static inline uint32_t tune_unpack(const uint32_t *w, const uint16_t *(*max_of)(uint32_t meter, uint32_t *np), uint32_t *meter, uint32_t *preset, uint8_t *idx, uint16_t *val)
{
    uint8_t b[16]; tune_from_words(w, b);
    uint32_t pos = 0; const uint32_t chk = tune_get(b, &pos, 8u); *meter = tune_get(b, &pos, 5u); *preset = tune_get(b, &pos, 4u); const uint32_t n = tune_get(b, &pos, 4u);
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
#endif
