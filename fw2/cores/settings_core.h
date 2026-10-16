/* fw2/cores/settings_core.h -- Player 2 settings table (spec section 10): ONE description of every persisted value (which register-file word, which bits, range, default). Persisted state,
 * runtime state and the menu come from this table, so they cannot drift apart. Pure, host-tested (sim/test_p2_settings.py).
 *
 * The words are the ones the old player already uses (fw/settings.inc; interact.json persist ids are the word numbers and NEVER change, so a saved setting survives an update):
 * word 0 volume, word 2 repeat, word 16 Halcyon preset, word 17 theme polarity (bit 0) and ReplayGain mode (bits 5:4). Pack touches only the bits of its own fields (a neighbour in the same word, such as the
 * polarity bit, is kept); unpack validates every field and falls back to the default for a value out of range (a stale or foreign value must never reach a setting). */
#ifndef SETTINGS_CORE_H
#define SETTINGS_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint8_t word, shift, width; uint32_t min, max, def; } sf_field_t;
enum { SF_VOLUME = 0, SF_REPEAT, SF_RGMODE, SF_HALCYON, SF_COUNT };
#define SF_WORDS 32u                   /* the register file: 32 words (the wide file; an older 16-word bitstream cannot hold word 17) */
static const sf_field_t sf_table[SF_COUNT] = {
    { 0u,  0u, 32u, 0u, 100u, 94u },   /* volume 0..100 (the dB taper position), default 94 */
    { 2u,  0u, 32u, 0u, 2u,   0u  },   /* repeat: 0 off, 1 all, 2 one */
    { 17u, 4u, 2u,  0u, 2u,   0u  },   /* ReplayGain mode: 0 off, 1 track, 2 album (3 is not a mode) */
    { 16u, 0u, 32u, 0u, 30u,  0u  },   /* Halcyon EQ preset (persist id 16, a slider 0..30): 0 off, 1..8 built-in, 9.. user presets */
};
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
}
#endif
