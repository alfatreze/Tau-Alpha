/* fw2/cores/audio_stats_core.h -- the audio statistics every meter may read (mtr_in_t: rms_l/rms_r, corr, centroid, crest, flux, onset), as pure integer functions.
 * Host-tested against float models (sim/test_p2_audio_stats.py); no 64-bit division (the core has none): sums are shifted down until a 32-bit divide or square root is exact enough.
 *
 * RMS and the L/R sums (sum L*L, sum R*R, sum L*R over n samples) come from the hardware block (tau_audio_stats.sv); centroid, crest, flux and onset are firmware-only, from numbers the host already has (the 16 bands, the peaks).
 *   rms:      raw sample units (0..32768); the host scales it by 3/4 like the peaks.
 *   corr:     Q8, -256 (opposite phase) .. 0 (unrelated) .. +256 (identical); 0 when either channel is silent.
 *   centroid: 0..255, the level-weighted mean band (0 = all in the lowest band, 255 = all in the highest); 0 for silence.
 *   crest:    Q4 peak / rms (16 = a square wave, 22 = a sine, more = spiky), 0 when silent, capped at 255 (15.9).
 *   flux/onset: spectral flux (the summed rise of the bands since the last picture, capped 255) and a hit flag against its own running mean (the detector Chladni uses, lifted out so every meter can react). */
#ifndef AUDIO_STATS_CORE_H
#define AUDIO_STATS_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define AS_BANDS 16u
static inline uint32_t as_isqrt32(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
static inline uint32_t as_rms(uint64_t sum, uint32_t n)
{
    if (!n) return 0;
    uint32_t sh = 0;
#if BUG != 1
    while (sum >> 32) { sum >>= 1; sh++; }                                                /* fit 32 bits; the mean is shifted back up after the divide (n is never rounded) */
#endif
    return as_isqrt32(((uint32_t)sum / n) << sh);
}
static inline int32_t as_corr_q8(uint64_t ll, uint64_t rr, int64_t lr)
{
    if (!ll || !rr) return 0;
    uint64_t m = ll > rr ? ll : rr; uint32_t k = 0;
    while (m >> 22) { m >>= 2; k += 2u; }                                                 /* even shifts: the square roots stay whole */
    const uint32_t l = (uint32_t)(ll >> k), r = (uint32_t)(rr >> k);
    const int32_t x = (int32_t)(lr / (int64_t)(1u << k));
    const uint32_t den = as_isqrt32(l) * as_isqrt32(r);
    if (!den) return 0;
#if BUG == 2
    int32_t c = -(x * 256) / (int32_t)den;
#else
    int32_t c = (x * 256) / (int32_t)den;
#endif
    return c > 256 ? 256 : c < -256 ? -256 : c;
}
static inline uint32_t as_centroid(const uint8_t *spec)
{
    uint32_t tot = 0, num = 0;
    for (uint32_t i = 0; i < AS_BANDS; i++) { tot += spec[i]; num += i * spec[i]; }
    if (!tot) return 0;
#if BUG == 3
    return (num * 255u) / (16u * tot);
#else
    return (num * 255u) / ((AS_BANDS - 1u) * tot);
#endif
}
static inline uint32_t as_crest_q4(uint32_t peak, uint32_t rms)
{
    if (!rms) return 0;
    const uint32_t c = (peak * 16u) / rms;
    return c > 255u ? 255u : c;
}
typedef struct { uint8_t prev[AS_BANDS]; int32_t ema_q8; uint32_t last_ms; uint8_t flux, onset; } as_onset_t;
#define AS_ONSET_SENS_Q4 24u                                                              /* a hit is a rise 1.5x its running mean (plus 6) */
#define AS_ONSET_REFRACT_MS 90u
static inline void as_onset_step(as_onset_t *s, const uint8_t *spec, uint32_t now_ms)
{
    uint32_t rise = 0;
    for (uint32_t b = 0; b < AS_BANDS; b++) { if (spec[b] > s->prev[b]) rise += (uint32_t)(spec[b] - s->prev[b]); s->prev[b] = spec[b]; }
    const uint32_t thr = ((AS_ONSET_SENS_Q4 * (uint32_t)s->ema_q8) >> 12) + 6u;
#if BUG == 4
    s->onset = rise > thr;
#else
    s->onset = rise > thr && (now_ms - s->last_ms) > AS_ONSET_REFRACT_MS;
#endif
    if (s->onset) s->last_ms = now_ms;
    s->ema_q8 += ((int32_t)(rise << 8) - s->ema_q8) >> 5;
    s->flux = (uint8_t)(rise > 255u ? 255u : rise);
}
#endif
