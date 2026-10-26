/* fw2/cores/settings_core.h -- Player 2 settings table (spec section 10): ONE description of every persisted value (which register-file word, which bits, range, default). Persisted state,
 * runtime state and the menu come from this table, so they cannot drift apart. Pure, host-tested (sim/test_p2_settings.py).
 *
 * The words are the ones the old player already uses (fw/settings.inc; interact.json persist ids are the word numbers and NEVER change, so a saved setting survives an update):
 * (a word's persist id is the word plus 10: word 0 = id 10 Volume, word 1 = id 11 Color, word 6 = id 16 Halcyon preset, word 16 = id 26 (internal) theme, word 17 = id 27 (internal) theme mode.) Words: 0 volume, 2 repeat, 6 Halcyon preset, 17 theme polarity (bit 0) and ReplayGain mode (bits 5:4). Pack touches only the bits of its own fields (a neighbour in the same word, such as the
 * polarity bit, is kept); unpack validates every field and falls back to the default for a value out of range (a stale or foreign value must never reach a setting). */
#ifndef SETTINGS_CORE_H
#define SETTINGS_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint8_t word, shift, width; uint32_t min, max, def; } sf_field_t;
/* THE LAYOUT (docs/features/PERSISTENCE_LAYOUT.md): one register-file word per area, the persist ids unchanged (tools/persist_registry.json records what each id means by version):
 *   word 0  Playback + layout version  (layout version 4 bits: 0 = the legacy layout of TAU DEV 181 and older, 1 = this one; which tuning slot is newer; room for shuffle, stretch, speed)
 *   word 1  Appearance                 (accent colour, theme, polarity, font)
 *   word 2  Audio                      (volume, repeat, ReplayGain, Halcyon preset)
 *   word 5  Meters 1                   (the selected meter, the presets of meters 0..5)       word 6 Meters 2 (meters 6..12)       word 15 Meters 3 (meters 13..19)
 *   words 8..11 and 16, 17, 18, 21     the tuning pool: two slots of four words (a tuning is the delta of ONE meter from its preset, cores/tune_core.h)
 *   words 19, 20                       the last played track (cores/lastplayed_core.h)
 * The Pocket takes 16 persist variables: this is all 16. A word's unused bits are free for later options of its area. */
#define SF_METERS_MAX 20u              /* meters with a persisted preset: 4 bits each in words 5, 6 and 15 */
enum { SF_VOLUME = 0, SF_REPEAT, SF_RGMODE, SF_HALCYON, SF_COLOUR, SF_THEME, SF_POLARITY, SF_METER, SF_FONT, SF_LAYOUT, SF_TUNE_NEW,
       SF_MPRE0, SF_MPRE1, SF_MPRE2, SF_MPRE3, SF_MPRE4, SF_MPRE5, SF_MPRE6, SF_MPRE7, SF_MPRE8, SF_MPRE9, SF_MPRE10, SF_MPRE11, SF_MPRE12, SF_MPRE13, SF_MPRE14, SF_MPRE15, SF_MPRE16, SF_MPRE17, SF_MPRE18, SF_MPRE19,
       SF_TUNE0, SF_TUNE1, SF_TUNE2, SF_TUNE3, SF_TUNE4, SF_TUNE5, SF_TUNE6, SF_TUNE7, SF_LAST0, SF_LAST1, SF_COUNT };
#define SF_LAYOUT_VERSION 1u
#define SF_WORDS 32u                   /* the register file: 32 words (the wide file; an older 16-word bitstream cannot hold word 16 and up) */
#define SF_T31(w) { w, 0u, 31u, 0u, 0x7FFFFFFFu, 0u }
static const sf_field_t sf_table[SF_COUNT] = {
    { 2u,  0u, 7u,  0u, 100u, 94u },   /* volume 0..100 (the dB taper position), default 94 */
    { 2u,  7u, 2u,  0u, 2u,   0u  },   /* repeat: 0 off, 1 all, 2 one */
    { 2u,  9u, 2u,  0u, 2u,   0u  },   /* ReplayGain mode: 0 off, 1 track, 2 album (3 is not a mode) */
    { 2u, 11u, 5u,  0u, 30u,  0u  },   /* Halcyon EQ preset: 0 off, 1..8 built-in, 9.. user presets */
    { 1u,  0u, 5u,  0u, 18u,  1u  },   /* accent colour: the 19 Analogue Pocket colours, default WHITE */
    { 1u,  5u, 6u,  0u, 31u,  0u  },   /* theme: built-in themes then the file themes */
    { 1u, 11u, 1u,  0u, 1u,   0u  },   /* mode: 0 dark, 1 light */
    { 5u,  0u, 6u,  0u, 62u,  0u  },   /* the meter: the index in the meter table */
    { 1u, 12u, 3u,  0u, 7u,   0u  },   /* the font: 0 = the font the bitstream powers up with (Tamzen Regular), 1 Tamzen Bold, 2 Space Grotesk SemiBold, 3 Space Grotesk Bold, 4..7 from tau-assets.bin */
    { 0u, 27u, 4u,  0u, 15u,  SF_LAYOUT_VERSION },   /* the layout version (0 = legacy: the word is a volume 0..100), a fresh card defaults to this layout */
    { 0u, 26u, 1u,  0u, 1u,   0u  },   /* which tuning slot was saved last (0 = slot A, words 8..11; 1 = slot B): the other one is evicted next */
    /* the selected preset of each meter in the table order, four bits each: word 5 holds meters 0..5 above the meter choice, word 6 meters 6..12, word 15 meters 13..19 */
    { 5u,  6u, 4u, 0u, 15u, 0u }, { 5u, 10u, 4u, 0u, 15u, 0u }, { 5u, 14u, 4u, 0u, 15u, 0u }, { 5u, 18u, 4u, 0u, 15u, 0u }, { 5u, 22u, 4u, 0u, 15u, 0u }, { 5u, 26u, 4u, 0u, 15u, 0u },
    { 6u,  0u, 4u, 0u, 15u, 0u }, { 6u,  4u, 4u, 0u, 15u, 0u }, { 6u,  8u, 4u, 0u, 15u, 0u }, { 6u, 12u, 4u, 0u, 15u, 0u }, { 6u, 16u, 4u, 0u, 15u, 0u }, { 6u, 20u, 4u, 0u, 15u, 0u }, { 6u, 24u, 4u, 0u, 15u, 0u },
    { 15u, 0u, 4u, 0u, 15u, 0u }, { 15u, 4u, 4u, 0u, 15u, 0u }, { 15u, 8u, 4u, 0u, 15u, 0u }, { 15u, 12u, 4u, 0u, 15u, 0u }, { 15u, 16u, 4u, 0u, 15u, 0u }, { 15u, 20u, 4u, 0u, 15u, 0u }, { 15u, 24u, 4u, 0u, 15u, 0u },
    /* the tuning pool: slot A = words 8..11 (persist ids 20..23), slot B = words 16, 17, 18, 21 (ids 27, 28, 29 and the new id 33); 31 bits each (cores/tune_core.h) */
    SF_T31(8u), SF_T31(9u), SF_T31(10u), SF_T31(11u), SF_T31(16u), SF_T31(17u), SF_T31(18u), SF_T31(21u),
    /* the last played track (cores/lastplayed_core.h): words 19 and 20 = persist ids 31 and 32 (a record that fails its check is ignored) */
    SF_T31(19u), SF_T31(20u),
};
/* Bit 30 of each word a legacy word is converted INTO (1 appearance, 2 audio, 5 meters 1, 6 meters 2, 15 meters 3) marks it as already in the new layout; no field reaches it. A migration that loses power half way is
 * resumed from that mark: the words are written in sf_write_order, chosen so that a legacy word is overwritten only after every new word that is built from it:
 *   w1 (colour, theme, polarity, font) is built from legacy w1, w16, w17, w18     w2 (volume, repeat, ReplayGain, Halcyon) from legacy w0, w2, w6, w17
 *   w5 (meter, presets 0..5) from legacy w5, w18      w6 (presets 6..12) from legacy w15, w18      w15 (presets 13..19) from legacy w18
 * so w1, w2, w5, w6, w15 first (w2 before w6, w6 before w15, all before w16..w18), then the words that become empty, the last played words (the same meaning in both layouts), and word 0 last. */
#define SF_WORD_NEW (1u << 30)
static const uint8_t sf_marked_words[] = { 1u, 2u, 5u, 6u, 15u };
static const uint8_t sf_write_order[] = { 1u, 2u, 5u, 6u, 15u, 16u, 17u, 18u, 21u, 8u, 9u, 10u, 11u, 19u, 20u };
/* ---- the LEGACY layout (TAU DEV 181 and older, layout version 0): read once to migrate ---- */
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
    for (uint32_t k = 0; k < sizeof sf_marked_words; k++) words[sf_marked_words[k]] |= SF_WORD_NEW;           /* a packed word is in the new layout (bit 30: the resume mark) */
}
/* ---- migration from the legacy layout (version 0): a word whose layout version is 0 is the old layout ---- */
static inline int sf_is_legacy(const uint32_t *words)
{
#if BUG == 5
    return 0;                                                            /* mutant: a legacy card is never migrated (its volume word is read as the new layout) */
#else
    return ((words[0] >> sf_table[SF_LAYOUT].shift) & sf_mask(&sf_table[SF_LAYOUT])) == 0u;
#endif
}
/* The values a legacy register file held (every field the legacy layout had, validated with its own ranges; the tuning of the old single slot is not carried over: its header differs and it is a
 * session convenience); every other field takes its default. */
static inline void sf_legacy_unpack(const uint32_t *words, uint32_t *vals)
{
    for (uint32_t i = 0; i < SF_COUNT; i++) vals[i] = sf_table[i].def;
    for (uint32_t k = 0; k < SF_LEGACY_N; k++) {
        const sf_legacy_t *l = &sf_legacy[k];
        const uint32_t m = l->width >= 32u ? 0xFFFFFFFFu : ((1u << l->width) - 1u);
        const uint32_t v = (words[l->word] >> l->shift) & m;
        vals[l->field] = (v < l->min || v > l->max) ? l->def : v;
    }
}
/* The register file of a migrated card: every word zero, then every field packed, so no legacy bit survives in a word whose meaning changed. Word 0 holds the layout version and is written last
 * by the caller (a power loss before that leaves the legacy marker, and the next boot migrates again from the still-valid legacy words). */
static inline void sf_migrate_words(const uint32_t *legacy_words, uint32_t *out)
{
    uint32_t vals[SF_COUNT]; sf_legacy_unpack(legacy_words, vals);
    for (uint32_t i = 0; i < SF_WORDS; i++) out[i] = 0u;
    sf_pack(out, vals);
}
/* The same for a register file a migration already half wrote: a word that carries the mark is kept as it is (its legacy value is gone, and nothing still needs it: see sf_write_order); the others are built
 * from the legacy words that are still there. */
static inline void sf_migrate_resume(const uint32_t *words, uint32_t *out)
{
    sf_migrate_words(words, out);
#if BUG != 6
    for (uint32_t k = 0; k < sizeof sf_marked_words; k++) if (words[sf_marked_words[k]] & SF_WORD_NEW) out[sf_marked_words[k]] = words[sf_marked_words[k]];
#endif
}
#endif
