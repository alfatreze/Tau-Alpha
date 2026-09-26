/* The golden model of the MP3 window unit's algorithm (B-292), shared by sim/mp3_poly_model.c (vs Helix's PolyphaseStereo) and the end-to-end
 * Subband harness sim/mp3_poly_subband_harness.c (B-307). Needs poly_tap_age[], poly_tap_p[], poly_coef_tab[] (tools/gen_mp3_poly_rom.py --h) included first. */
#ifndef MP3_POLY_MODEL_CORE_H
#define MP3_POLY_MODEL_CORE_H
#include <stdint.h>
#include <string.h>
#define CSHIFT 12
#define NFRAC  6
static int ring[2][16][32];                 /* [channel][slot slot index (head - age) & 15][P] */
static int head[2];
static short clip16(int64_t sum)
{
    int x = (int)(sum >> (32 - CSHIFT));    /* the (int) cast truncates to 32 bits, as Helix's does */
    x >>= NFRAC;
    int sign = x >> 31;
    if (sign != (x >> 15)) x = sign ^ ((1 << 15) - 1);
    return (short)x;
}
static int word(int ch, int row, int tap)
{
    int i = row * 16 + tap;
    return ring[ch][(head[ch] - poly_tap_age[i]) & 15][poly_tap_p[i]];
}
static void push(int ch, const int *w)      /* a new slot: it becomes age 0 */
{
    head[ch] = (head[ch] + 1) & 15;
    memcpy(ring[ch][head[ch]], w, 32 * sizeof(int));
}
static void window(int ch, short *out /* 32 samples, stride 1 */)
{
    const int64_t rnd = (int64_t)1 << (NFRAC - 1 + (32 - CSHIFT));
    int64_t sum = rnd;
    for (int x = 0; x < 8; x++) {           /* sample 0: MC0 */
        int c1 = poly_coef_tab[2 * x], c2 = poly_coef_tab[2 * x + 1];
        sum += (int64_t)word(ch, 0, x) * c1;
        sum += (int64_t)word(ch, 0, 8 + x) * -c2;
    }
    out[0] = clip16(sum);
    sum = rnd;                              /* sample 16: MC1, coefficients 256..263 */
    for (int x = 0; x < 8; x++) sum += (int64_t)word(ch, 16, x) * poly_coef_tab[256 + x];
    out[16] = clip16(sum);
    for (int i = 1; i < 16; i++) {          /* samples i and 32 - i */
        int64_t s1 = rnd, s2 = rnd;
        for (int x = 0; x < 8; x++) {
            int c1 = poly_coef_tab[16 + (i - 1) * 16 + 2 * x], c2 = poly_coef_tab[16 + (i - 1) * 16 + 2 * x + 1];
            int lo = word(ch, i, x), hi = word(ch, i, 8 + x);
            s1 += (int64_t)lo * c1; s2 += (int64_t)lo * c2;
            s1 += (int64_t)hi * -c2; s2 += (int64_t)hi * c1;
        }
        out[i] = clip16(s1); out[32 - i] = clip16(s2);
    }
}

#endif
