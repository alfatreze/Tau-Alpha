/* fw/pcm_push.h -- the pure part of the one shared PCM push path (Cymo C1, B-533).
 *
 * MP3 (the decode loop) and FLAC (flac_emit) each carried their own copy of: volume, then the discontinuity fade-in, then pack and write. They had already drifted
 * apart once (FLAC shipped with no volume at all). This header holds the arithmetic exactly as it was in both copies, so it can be host-tested against the old
 * formula; the FIFO wait and the MMIO write stay in player.c's cymo_push(). Volume first, then the fade, so a fade-in at low volume stays at low volume. Capped
 * at unity: it only ever attenuates, so no clamp is needed. */
#ifndef PCM_PUSH_H
#define PCM_PUSH_H
#include <stdint.h>

static inline void pcm_gain_apply(int32_t *l, int32_t *r, int32_t vol_gain, uint32_t *fade_left, uint32_t fade_samples)
{
    if (vol_gain != 256) {
        *l = (*l * vol_gain) >> 8;
        *r = (*r * vol_gain) >> 8;
    }
    if (*fade_left) {
        int32_t g = (int32_t)((fade_samples - *fade_left) >> 3);
        *l = (*l * g) >> 8;
        *r = (*r * g) >> 8;
        (*fade_left)--;
    }
}

static inline uint32_t pcm_pack(int32_t l, int32_t r)
{
    return ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
}
#endif
