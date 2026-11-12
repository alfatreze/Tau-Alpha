/* fw2/cores/settings_core.h -- Player 2 settings table (spec section 10): ONE description of every persisted value (which register-file word, which bits, range, default). Persisted state,
 * runtime state and the menu come from this table, so they cannot drift apart. Pure, host-tested (sim/test_p2_settings.py).
 *
 * The words are the ones the old player already uses (fw/settings.inc; interact.json persist ids are the word numbers and NEVER change, so a saved setting survives an update):
 * (a word's persist id is the word plus 10: word 0 = id 10 Volume, word 1 = id 11 Color, word 6 = id 16 Halcyon preset, word 16 = id 26 (internal) theme, word 17 = id 27 (internal) theme mode.) Words: 0 volume, 2 repeat, 6 Halcyon preset, 17 theme polarity (bit 0) and ReplayGain mode (bits 5:4). Pack touches only the bits of its own fields (a neighbour in the same word, such as the
 * polarity bit, is kept); unpack validates every field and falls back to the default for a value out of range (a stale or foreign value must never reach a setting). */
#ifndef SETTINGS_CORE_H
#define SETTINGS_CORE_H
#include <stdint.h>
#include "tune_core.h"
#include "shipped_meter_ids.h"
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint8_t word, shift, width; uint32_t min, max, def; } sf_field_t;
/* THE LAYOUT (docs/features/PERSISTENCE_LAYOUT.md): one register-file word per area, the persist ids unchanged (tools/persist_registry.json records what each id means by version):
 *   word 0  Playback + layout version  (layout version 4 bits: 0 = the legacy layout of TAU DEV 181 and older, 1 = the layout by area (TAU DEV 182 .. 219, v0.7.x), 2 = this one; which tuning slot is newer; room for shuffle, stretch, speed)
 *   word 1  Appearance                 (accent colour, theme, polarity, font)
 *   word 2  Audio                      (volume, repeat, ReplayGain, Halcyon preset)
 *   word 5  The selected meter         (its id_hash, 31 bits: the identity of an external meter, EXTERNAL_METERS_SPEC.md section 5; 0 = none = Winamp Bars)
 *   word 6  Its preset                 (the position in the pack's own list 4 bits, the preset file's id + 1 7 bits; scratch bits 16..11 for a resumed migration; bit 29 marks the word converted)
 *   word 15                            FREE (layout 1 held the preset ids of meters 3..6)
 *   words 8..11 and 16, 17, 18, 21     the tuning pool: two slots of four words (a tuning is the delta of ONE meter from its preset, keyed by id_hash low 16 and params_rev low 4: cores/tune_core.h)
 *   words 19, 20                       the last played track (cores/lastplayed_core.h)
 * The Pocket takes 16 persist variables: this is all 16. A word's unused bits are free for later options of its area. */
#define SF_METERS_MAX 7u               /* the shipped meters of layout 0 and 1 (cores/shipped_meter_ids.h): the table indexes a migration maps to an id_hash; appearance.c asserts the registry fits (until P2.4 replaces it) */
enum { SF_VOLUME = 0, SF_REPEAT, SF_RGMODE, SF_HALCYON, SF_COLOUR, SF_THEME, SF_POLARITY, SF_METER_HASH, SF_FONT, SF_LAYOUT, SF_TUNE_NEW, SF_METER_PRE_POS, SF_METER_PRE_ID,
       SF_TUNE0, SF_TUNE1, SF_TUNE2, SF_TUNE3, SF_TUNE4, SF_TUNE5, SF_TUNE6, SF_TUNE7,
       /* COMPATIBILITY entries (no storage, width 0: they always read 0 and are never written): the layout-1 names fw2/ui/appearance.c still uses, kept so it compiles until P2.4 replaces its call sites; the legacy reader
        * also uses them as scratch for the layout-0 values it maps. */
       SF_METER, SF_MPRE0, SF_MPRE1, SF_MPRE2, SF_MPRE3, SF_MPRE4, SF_MPRE5, SF_MPRE6, SF_MPID0, SF_MPID1, SF_MPID2, SF_MPID3, SF_MPID4, SF_MPID5, SF_MPID6,
       SF_LAST0, SF_LAST1, SF_COUNT };
#define SF_LAYOUT_VERSION 2u
#define SF_WORDS 32u                   /* the register file: 32 words (the wide file; an older 16-word bitstream cannot hold word 16 and up) */
#define SF_T31(w) { w, 0u, 31u, 0u, 0x7FFFFFFFu, 0u }
#define SF_NONE { 0u, 0u, 0u, 0u, 0u, 0u }
static const sf_field_t sf_table[SF_COUNT] = {
    { 2u,  0u, 7u,  0u, 100u, 94u },   /* volume 0..100 (the dB taper position), default 94 */
    { 2u,  7u, 2u,  0u, 2u,   0u  },   /* repeat: 0 off, 1 all, 2 one */
    { 2u,  9u, 2u,  0u, 2u,   0u  },   /* ReplayGain mode: 0 off, 1 track, 2 album (3 is not a mode) */
    { 2u, 11u, 5u,  0u, 30u,  0u  },   /* Halcyon EQ preset: 0 off, 1..8 built-in, 9.. user presets */
    { 1u,  0u, 5u,  0u, 18u,  1u  },   /* accent colour: the 19 Analogue Pocket colours, default WHITE */
    { 1u,  5u, 6u,  0u, 31u,  0u  },   /* theme: built-in themes then the file themes */
    { 1u, 11u, 1u,  0u, 1u,   0u  },   /* mode: 0 dark, 1 light */
    SF_T31(5u),                        /* the selected meter: the low 31 bits of its id_hash (0 = none: Winamp Bars) */
    { 1u, 12u, 3u,  0u, 7u,   0u  },   /* the font: 0 = the font the bitstream powers up with (Tamzen Regular), 1 Tamzen Bold, 2 Space Grotesk SemiBold, 3 Space Grotesk Bold, 4..7 from tau-assets.bin */
    { 0u, 27u, 4u,  0u, 15u,  SF_LAYOUT_VERSION },   /* the layout version (0 = legacy: the word is a volume 0..100), a fresh card defaults to this layout */
    { 0u, 26u, 1u,  0u, 1u,   0u  },   /* which tuning slot was saved last (0 = slot A, words 8..11; 1 = slot B): the other one is evicted next */
    { 6u,  0u, 4u, 0u, 15u,  0u },     /* the selected meter's preset POSITION in the pack's own list (used when the meter has no preset files) */
    { 6u,  4u, 7u, 0u, 127u, 0u },     /* the preset FILE the selected meter was set to: 0 = none (the meter's defaults), else the preset's id + 1 (ids 0..126) */
    /* the tuning pool: slot A = words 8..11 (persist ids 20..23), slot B = words 16, 17, 18, 21 (ids 27, 28, 29 and 33); 31 bits each (cores/tune_core.h) */
    SF_T31(8u), SF_T31(9u), SF_T31(10u), SF_T31(11u), SF_T31(16u), SF_T31(17u), SF_T31(18u), SF_T31(21u),
    SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE, SF_NONE,   /* SF_METER, SF_MPRE0..6, SF_MPID0..6: no storage */
    /* the last played track (cores/lastplayed_core.h): words 19 and 20 = persist ids 31 and 32 (a record that fails its check is ignored) */
    SF_T31(19u), SF_T31(20u),
};
/* THE MARKS. Bit 30 of words 1 and 2 (a legacy word is converted INTO them from layout 0) and bit 29 of word 6 mark a word as already in layout 2; no field reaches them. Word 5 holds 31 bits of hash and cannot carry one, so word 6
 * keeps, below its fields, the old meter INDEX (bits 16..11, scratch): a migration that loses power between word 6 and word 5 is resumed by rebuilding word 5 from that index. A migration (layout 0 or 1 -> 2) writes the words in
 * sf_write_order, chosen so that a source word is overwritten only after every word built from it:
 *   layout 0: w1 (colour, theme, polarity, font) from legacy w1, w16, w17, w18    w2 (volume, repeat, ReplayGain, Halcyon) from legacy w0, w2, w6, w17
 *             w6 (selected preset + scratch) from legacy w5, w18, w15             w5 (hash) from legacy w5 (or the scratch of w6)
 *   layout 1: w6 (selected preset + scratch) from w5, w6, w15                     w5 (hash) from w5 (or the scratch of w6)     w15 emptied after w6;  tuning slots B (16, 17, 18, 21) and A (8..11) converted slot by slot
 * so w1, w2, w6 first, then w5, w15, the words that become empty or converted (16, 17, 18, 21, 8..11), the last played words (the same meaning in every layout), and word 0 last (the version: a power loss before that leaves the old
 * version, and the next boot resumes). A power loss INSIDE a tuning slot (four writes) leaves a record neither layout accepts: that one tuning is lost (the owner's rule: at worst a meter's settings are lost). */
#define SF_WORD_NEW (1u << 30)
#define SF_MARK6 (1u << 29)
#define SF_SCRATCH_SHIFT 11u
static const uint8_t sf_mark_word[] = { 1u, 2u, 6u };
static const uint8_t sf_mark_bit[]  = { 30u, 30u, 29u };
static const uint8_t sf_write_order[] = { 1u, 2u, 6u, 5u, 15u, 16u, 17u, 18u, 21u, 8u, 9u, 10u, 11u, 19u, 20u };
/* ---- the LEGACY layout (TAU DEV 181 and older, layout version 0): read once to migrate. The meter and its presets are read into the compatibility slots (scratch) ---- */
typedef struct { uint8_t field, word, shift, width; uint32_t min, max, def; } sf_legacy_t;
static const sf_legacy_t sf_legacy[] = {
    { SF_VOLUME,  0u,  0u, 32u, 0u, 100u, 94u }, { SF_REPEAT, 2u, 0u, 32u, 0u, 2u, 0u }, { SF_RGMODE, 17u, 4u, 2u, 0u, 2u, 0u }, { SF_HALCYON, 6u, 0u, 32u, 0u, 30u, 0u },
    { SF_COLOUR,  1u,  0u, 32u, 0u, 18u, 1u }, { SF_THEME, 16u, 0u, 32u, 0u, 31u, 0u }, { SF_POLARITY, 17u, 0u, 1u, 0u, 1u, 0u }, { SF_METER, 5u, 0u, 32u, 0u, 14u, 0u },
    { SF_MPRE0, 18u, 0u, 4u, 0u, 7u, 0u }, { SF_MPRE0 + 1, 18u, 4u, 4u, 0u, 7u, 0u }, { SF_MPRE0 + 2, 18u, 8u, 4u, 0u, 7u, 0u }, { SF_MPRE0 + 3, 18u, 12u, 4u, 0u, 15u, 0u }, { SF_MPRE0 + 4, 18u, 16u, 4u, 0u, 7u, 0u },
    { SF_FONT, 18u, 20u, 3u, 0u, 7u, 0u }, { SF_MPRE0 + 5, 18u, 23u, 4u, 0u, 7u, 0u }, { SF_MPRE0 + 6, 15u, 0u, 4u, 0u, 15u, 0u },
    { SF_LAST0, 19u, 0u, 31u, 0u, 0x7FFFFFFFu, 0u }, { SF_LAST1, 20u, 0u, 31u, 0u, 0x7FFFFFFFu, 0u },
};
#define SF_LEGACY_N (sizeof sf_legacy / sizeof sf_legacy[0])
#define SF_LAZY_FIRST SF_LAST0           /* the fields from here on are saved only when nothing is streaming, or after a long delay: a track change must not disturb the card stream with an APF save */
/* ---- layout 1 (TAU DEV 182 .. 219, v0.7.x): only the meter parts differ from layout 2. word, shift, width ---- */
static const uint8_t sf_v1_meter[3] = { 5u, 0u, 6u };                                          /* the selected meter's table index, 0..62 */
static const uint8_t sf_v1_mpre[SF_METERS_MAX][3] = { { 5u, 6u, 4u }, { 5u, 10u, 4u }, { 5u, 14u, 4u }, { 5u, 18u, 4u }, { 5u, 22u, 4u }, { 5u, 26u, 4u }, { 6u, 0u, 4u } };      /* preset positions */
static const uint8_t sf_v1_mpid[SF_METERS_MAX][3] = { { 6u, 4u, 7u }, { 6u, 11u, 7u }, { 6u, 18u, 7u }, { 15u, 0u, 7u }, { 15u, 7u, 7u }, { 15u, 14u, 7u }, { 15u, 21u, 7u } };    /* preset file ids + 1 */
static inline uint32_t sf_mask(const sf_field_t *f) { return f->width >= 32u ? 0xFFFFFFFFu : ((1u << f->width) - 1u); }
static inline uint32_t sf_unpack1(const uint32_t *words, uint32_t i)
{
    const sf_field_t *f = &sf_table[i];
#if BUG == 1
    uint32_t v = (words[f->word] >> (f->shift + 1u)) & sf_mask(f);       /* shift off by one */
#elif BUG == 2
    uint32_t v = (words[f->word] >> f->shift);                           /* no mask: the neighbouring bits leak in */
#else
    uint32_t v = (words[f->word] >> f->shift) & sf_mask(f);
#endif
#if BUG != 3
    if (v < f->min || v > f->max) v = f->def;                            /* a value out of range is not a setting */
#endif
    return v;
}
static inline void sf_unpack(const uint32_t *words, uint32_t *vals) { for (uint32_t i = 0; i < SF_COUNT; i++) vals[i] = sf_unpack1(words, i); }
/* Merge the values into the words, keeping every other bit. */
static inline void sf_pack(uint32_t *words, const uint32_t *vals)
{
    for (uint32_t i = 0; i < SF_COUNT; i++) {
        const sf_field_t *f = &sf_table[i];
        uint32_t v = vals[i]; if (v > f->max) v = f->max; if (v < f->min) v = f->min;
        const uint32_t m __attribute__((unused)) = sf_mask(f) << (f->shift & 31u);
#if BUG == 4
        words[f->word] = (v << f->shift);                                /* clobbers the neighbours (the theme polarity bit) */
#else
        words[f->word] = (words[f->word] & ~m) | ((v << f->shift) & m);
#endif
    }
    for (uint32_t k = 0; k < sizeof sf_mark_word; k++) words[sf_mark_word[k]] |= 1u << sf_mark_bit[k];             /* a packed word is in layout 2 (the resume marks) */
}
/* ---- migration from layout 0 or 1 (version 0 or 1 in word 0) to layout 2 ---- */
static inline int sf_is_legacy(const uint32_t *words)
{
#if BUG == 5
    return 0;                                                            /* mutant: a legacy card is never migrated (its volume word is read as the new layout) */
#else
    return ((words[0] >> sf_table[SF_LAYOUT].shift) & sf_mask(&sf_table[SF_LAYOUT])) == 0u;
#endif
}
static inline int sf_is_v1(const uint32_t *words) { return ((words[0] >> sf_table[SF_LAYOUT].shift) & sf_mask(&sf_table[SF_LAYOUT])) == 1u; }
static inline int sf_needs_migration(const uint32_t *words) { return sf_is_legacy(words) || sf_is_v1(words); }
/* the identity of a shipped meter by its layout-0/1 table index: the 31 bits that are persisted (0 = none for an index that is not a shipped meter), and the key of its tunings (TUNE_KEY_NONE for none) */
static inline uint32_t sf_hash31_of_index(uint32_t i)
{
#if BUG == 7
    return i < SHIPPED_METERS_N ? (shipped_meter_hash[i] & 0x7FFFFFFFu) >> 1 : 0u;           /* mutant: the hash loses a bit */
#else
    return i < SHIPPED_METERS_N ? (shipped_meter_hash[i] & 0x7FFFFFFFu) : 0u;
#endif
}
static inline uint32_t sf_key_of_index(uint32_t i)
{
#if BUG == 8
    return i < SHIPPED_METERS_N ? (shipped_meter_hash[i] & 0xFFFFu) : TUNE_KEY_NONE;         /* mutant: the key forgets params_rev */
#else
    return i < SHIPPED_METERS_N ? tune_key(shipped_meter_hash[i], shipped_meter_rev[i]) : TUNE_KEY_NONE;
#endif
}
typedef const uint16_t *(*sf_max_of_t)(uint32_t meter, uint32_t *np);
/* The values a LEGACY register file held, every field validated with its own ranges, the other fields at their default. The legacy meter index and preset positions are left in the compatibility slots (SF_METER,
 * SF_MPRE0..6) for sf_migrate_words_t; sf_legacy_unpack clears them. The tuning of the old single slot is not carried over (its header differs and it is a session convenience). */
static inline void sf_legacy_unpack_raw(const uint32_t *words, uint32_t *vals)
{
    for (uint32_t i = 0; i < SF_COUNT; i++) vals[i] = sf_table[i].def;
    for (uint32_t k = 0; k < SF_LEGACY_N; k++) {
        const sf_legacy_t *l = &sf_legacy[k];
        const uint32_t m = l->width >= 32u ? 0xFFFFFFFFu : ((1u << l->width) - 1u);
        const uint32_t v = (words[l->word] >> l->shift) & m;
        vals[l->field] = (v < l->min || v > l->max) ? l->def : v;
    }
    const uint32_t idx = vals[SF_METER];
    vals[SF_METER_HASH] = sf_hash31_of_index(idx);
    vals[SF_METER_PRE_POS] = idx < SF_METERS_MAX ? vals[SF_MPRE0 + idx] : 0u;
    vals[SF_METER_PRE_ID] = 0u;
}
static inline void sf_legacy_unpack(const uint32_t *words, uint32_t *vals)
{
    sf_legacy_unpack_raw(words, vals);
    vals[SF_METER] = 0u; for (uint32_t m = 0; m < SF_METERS_MAX; m++) { vals[SF_MPRE0 + m] = 0u; vals[SF_MPID0 + m] = 0u; }
}
static inline uint32_t sf_v1_get(const uint32_t *w, const uint8_t *spec, uint32_t max)
{
    const uint32_t v = (w[spec[0]] >> spec[1]) & ((1u << spec[2]) - 1u);
    return v > max ? 0u : v;
}
/* The register file of a migrated card (layout 2). max_of(meter index) gives the largest value of each parameter of a shipped meter: it is what lets a layout-1 tuning be read and kept; 0 drops every tuning.
 * Layout 0: every word zero, then every field packed, so no legacy bit survives in a word whose meaning changed (the legacy tuning is dropped). Layout 1: the words that did not change are kept, word 5 becomes the hash of
 * the shipped meter its index named, word 6 the selected meter's preset (position, file id + 1) with the old index as scratch, word 15 is emptied, the tuning slots are re-keyed (tune_convert_v1); the per-meter preset
 * positions and ids of the other meters are dropped. Word 0 holds the layout version and is written last by the caller. */
static inline void sf_migrate_words_t(const uint32_t *words, uint32_t *out, sf_max_of_t max_of)
{
    if (sf_is_legacy(words)) {
        uint32_t vals[SF_COUNT]; sf_legacy_unpack_raw(words, vals);
        const uint32_t idx = vals[SF_METER];
        for (uint32_t i = 0; i < SF_WORDS; i++) out[i] = 0u;
        sf_pack(out, vals);
        out[6] |= (idx < 63u ? idx : 63u) << SF_SCRATCH_SHIFT;
        return;
    }
    for (uint32_t i = 0; i < SF_WORDS; i++) out[i] = words[i];
    uint32_t idx = sf_v1_get(words, sf_v1_meter, 62u), pos = 0u, pid = 0u;
    if (idx < SF_METERS_MAX) { pos = sf_v1_get(words, sf_v1_mpre[idx], 15u); pid = sf_v1_get(words, sf_v1_mpid[idx], 127u); }
    out[5] = sf_hash31_of_index(idx);
    out[6] = pos | (pid << 4) | ((idx < 63u ? idx : 63u) << SF_SCRATCH_SHIFT) | SF_MARK6;
    out[15] = 0u;
    static const uint8_t slots[2][TUNE_WORDS] = { { 8u, 9u, 10u, 11u }, { 16u, 17u, 18u, 21u } };
    for (uint32_t s = 0; s < 2u; s++) {
        uint32_t in4[TUNE_WORDS], out4[TUNE_WORDS];
        for (uint32_t k = 0; k < TUNE_WORDS; k++) in4[k] = words[slots[s][k]];
        tune_convert_v1(in4, max_of, sf_key_of_index, out4);
        for (uint32_t k = 0; k < TUNE_WORDS; k++) out[slots[s][k]] = out4[k];
    }
    out[0] = (words[0] & ~(sf_mask(&sf_table[SF_LAYOUT]) << sf_table[SF_LAYOUT].shift)) | (SF_LAYOUT_VERSION << sf_table[SF_LAYOUT].shift);
}
/* The same for a register file a migration already half wrote (it writes sf_write_order, word 0 last, so the version is still 0 or 1): word 6 carrying its mark is kept and word 5 is rebuilt from the index it keeps; (layout 0)
 * a word 1 or 2 with its mark is kept; (layout 1) a tuning slot already converted is kept (tune_convert_v1). */
static inline void sf_migrate_resume_t(const uint32_t *words, uint32_t *out, sf_max_of_t max_of)
{
    sf_migrate_words_t(words, out, max_of);
#if BUG != 6
    if (sf_is_legacy(words)) for (uint32_t k = 0; k < 2u; k++) if (words[sf_mark_word[k]] & SF_WORD_NEW) out[sf_mark_word[k]] = words[sf_mark_word[k]];
    if (words[6] & SF_MARK6) { out[6] = words[6]; out[5] = sf_hash31_of_index((words[6] >> SF_SCRATCH_SHIFT) & 63u); }
#endif
}
/* the entry points that existed before layout 2: no tuning table, so every layout-1 tuning is dropped */
static inline void sf_migrate_words(const uint32_t *words, uint32_t *out) { sf_migrate_words_t(words, out, 0); }
static inline void sf_migrate_resume(const uint32_t *words, uint32_t *out) { sf_migrate_resume_t(words, out, 0); }
#endif
