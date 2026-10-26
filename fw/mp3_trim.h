/* fw/mp3_trim.h -- the gapless trim of an MP3 (parallel plan A4 / Cymo C6, B-737). Pure arithmetic, host-tested (sim/test_mp3_trim.py).
 *
 * A LAME file's Info frame carries the encoder delay and end padding. The decoded stream (Helix, as the firmware runs it) is: the Info frame itself (one frame of silence, `spf`
 * pairs), then the audio frames. The real music starts `delay + 529` pairs into the AUDIO frames (529 = the decoder's own delay) and there are `frames * spf - delay - padding`
 * of it, where `frames` is the Xing/Info frame count (which does not count the Info frame). Proven on six Test Album MP3s against the FLAC of the same recordings: the trimmed
 * length equals the FLAC's sample count exactly and the waveforms line up at lag 0 (SNR 23 to 49 dB for 128 and 320 kbps) at both ends.
 * In decoded-stream coordinates (pair index from the first decoded frame, the Info frame included): keep [S, S + P) with S = spf + delay + 529 and P = frames*spf - delay - padding. */
#ifndef MP3_TRIM_H
#define MP3_TRIM_H
#include <stdint.h>

typedef struct { uint32_t S, P, spf; } mp3_trim_cfg_t;

/* 1 when the file can be trimmed (a LAME-style tag gave delay and padding, a frame count is known and the numbers are sane), else 0 and nothing is trimmed. */
static inline int mp3_trim_cfg(mp3_trim_cfg_t *c, uint32_t frames, uint32_t spf, uint32_t delay, uint32_t padding)
{
    if (!frames || !spf || (!delay && !padding)) return 0;
    const uint32_t total = frames * spf;
    if (padding < 529u || delay + padding >= total) return 0;   /* padding below the decoder's own delay (529) means the stream would end before the kept range does: an odd file, play it untouched */
    c->spf = spf; c->S = spf + delay + 529u; c->P = total - delay - padding;
    return 1;
}

/* The part of decoded frame `fi` (0 = the Info frame, counted from the first frame the decoder produced) to play: pairs [*first, *first + *count). */
static inline void mp3_trim_range(const mp3_trim_cfg_t *c, uint32_t fi, uint32_t *first, uint32_t *count)
{
    const uint32_t f0 = fi * c->spf, f1 = f0 + c->spf, e = c->S + c->P;
    const uint32_t lo = f0 > c->S ? f0 : c->S, hi = f1 < e ? f1 : e;
    if (hi > lo) { *first = lo - f0; *count = hi - lo; } else { *first = 0; *count = 0; }
}
#endif
