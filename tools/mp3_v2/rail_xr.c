/* MP3 v2 WP-04: spectral headroom probe. Own code (Tau Alpha, MIT) around a PROBED COPY of the pristine minimp3 header: tools/mp3_v2/rail_count.py writes
 * build/mp3_v2/xr_probe/minimp3.h = third_party/minimp3/minimp3.h with two insertions in L3_decode (a global maximum and a histogram of |xr| after
 * requantisation and stereo processing, before reorder, antialias and the IMDCT); the vendored file is never touched, and the build is rejected unless
 * the copy differs from it only by those insertions (the python side checks).
 *
 *   rail_xr in.mp3        -> one JSON line: xr_max (minimp3's float units: 1.0 is the float PCM full scale, the 16-bit rail), xr_max_over_32768 (the same
 *                            number scaled to 16-bit PCM units), granule-channels counted, histogram of the per-granule-channel maximum by power of two
 *                            (bucket 0: max < 2^-19; bucket e = 1..23: 2^(e-20) <= max < 2^(e-19); bucket 24: max >= 2^4), float PCM peak (cross-check with the oracle).
 * Decoding is the oracle's (same flags, same tag stripping), so the PCM is the oracle's. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(sz ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)sz;
    return b;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: rail_xr in.mp3\n"); return 2; }
    size_t n; uint8_t *buf = slurp(argv[1], &n);
    if (!buf) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    const uint8_t *p = buf; size_t left = n;
    if (left >= 10 && !memcmp(p, "ID3", 3)) {
        size_t sz = 10 + (((size_t)p[6] << 21) | ((size_t)p[7] << 14) | ((size_t)p[8] << 7) | p[9]) + ((p[5] & 0x10) ? 10 : 0);
        if (sz > left) sz = left;
        p += sz; left -= sz;
    }
    if (left >= 128 && !memcmp(p + left - 128, "TAG", 3)) left -= 128;
    if (left >= 32 && !memcmp(p + left - 32, "APETAGEX", 8)) {
        uint32_t sz = (uint32_t)p[left - 20] | ((uint32_t)p[left - 19] << 8) | ((uint32_t)p[left - 18] << 16) | ((uint32_t)p[left - 17] << 24);
        uint32_t fl = (uint32_t)p[left - 12] | ((uint32_t)p[left - 11] << 8) | ((uint32_t)p[left - 10] << 16) | ((uint32_t)p[left - 9] << 24);
        size_t cut = (size_t)sz + ((fl & 0x80000000u) ? 32 : 0);
        left = cut <= left ? left - cut : 0;
    }
    static mp3dec_t dec; mp3dec_init(&dec);
    static float pcmf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t info; double peak = 0; unsigned long long pairs = 0;
    while (left > 0) {
        int s = mp3dec_decode_frame(&dec, p, (int)left, pcmf, &info);
        if (!info.frame_bytes) break;
        p += info.frame_bytes; left -= (size_t)info.frame_bytes;
        if (s <= 0) continue;
        pairs += (unsigned)s;
        for (int i = 0; i < s * info.channels; i++) { double a = fabs((double)pcmf[i]); if (a > peak) peak = a; }
    }
    printf("{\"xr_max\":%.9f,\"xr_max_16bit\":%.6f,\"granule_channels\":%llu,\"hist\":[", (double)tau_xr_max, (double)tau_xr_max * 32768.0, tau_xr_count);
    for (int e = 0; e < 25; e++) printf("%s%llu", e ? "," : "", tau_xr_hist[e]);
    printf("],\"pairs\":%llu,\"peak\":%.9f}\n", pairs, peak);
    free(buf);
    return 0;
}
