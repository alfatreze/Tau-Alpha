/* MP3 v2 float oracle: a command-line decoder around the PRISTINE lieff/minimp3 (CC0 1.0, third_party/minimp3/minimp3.h, unchanged).
 * Own code (Tau Alpha, MIT). Decodes a whole MP3 file to 16-bit PCM and/or a float dump, deterministically:
 *   oracle <in.mp3> [--pcm out.s16le] [--f32 out.f32le] [--frames N]
 * Always prints one JSON line to stdout: frames, decoded frames, hz, channels, sample pairs, float peak (1.0 = 16-bit full scale), count of
 * samples beyond +-1.0 (clipping potential of the float output), count of rail samples in the 16-bit output.
 * Build (tools/mp3_v2/oracle.py does it): cc -O2 -ffp-contract=off -DMINIMP3_NO_SIMD -DMINIMP3_ONLY_MP3 -DMINIMP3_FLOAT_OUTPUT ...
 *   -ffp-contract=off so an FMA-capable host (arm64) cannot change the float results: output is bit-identical run to run and host to host.
 * The 16-bit output is mp3dec_f32_to_s16 of the float output (minimp3's own conversion, scalar path), which equals the library's integer
 * output (cross-checked once against a build without MINIMP3_FLOAT_OUTPUT, result in the WP-01 report). */
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
    const char *in = NULL, *pcm_path = NULL, *f32_path = NULL;
    long max_frames = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pcm") && i + 1 < argc) pcm_path = argv[++i];
        else if (!strcmp(argv[i], "--f32") && i + 1 < argc) f32_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atol(argv[++i]);
        else if (!in) in = argv[i];
        else { fprintf(stderr, "usage: oracle in.mp3 [--pcm out] [--f32 out] [--frames N]\n"); return 2; }
    }
    if (!in) { fprintf(stderr, "usage: oracle in.mp3 [--pcm out] [--f32 out] [--frames N]\n"); return 2; }
    size_t n; uint8_t *buf = slurp(in, &n);
    if (!buf) { fprintf(stderr, "cannot read %s\n", in); return 2; }
    FILE *fp = pcm_path ? fopen(pcm_path, "wb") : NULL, *ff = f32_path ? fopen(f32_path, "wb") : NULL;
    if ((pcm_path && !fp) || (f32_path && !ff)) { fprintf(stderr, "cannot open output\n"); return 2; }

    const uint8_t *p = buf; size_t left = n;
    if (left >= 10 && !memcmp(p, "ID3", 3)) {   /* ID3v2: skip by the declared synchsafe size (+10 for the footer flag) */
        size_t sz = 10 + (((size_t)p[6] << 21) | ((size_t)p[7] << 14) | ((size_t)p[8] << 7) | p[9]) + ((p[5] & 0x10) ? 10 : 0);
        if (sz > left) sz = left;
        p += sz; left -= sz;
    }
    static mp3dec_t dec; mp3dec_init(&dec);
    static float pcmf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    static int16_t pcm16[MINIMP3_MAX_SAMPLES_PER_FRAME];
    long frames = 0, decoded = 0; unsigned long long pairs = 0, clip_f = 0, rail16 = 0; double peak = 0; int hz = 0, ch = 0;
    mp3dec_frame_info_t info;
    while (left > 0 && (max_frames < 0 || frames < max_frames)) {
        int s = mp3dec_decode_frame(&dec, p, (int)left, pcmf, &info);
        if (!info.frame_bytes) break;
        frames++;
        p += info.frame_bytes; left -= (size_t)info.frame_bytes;
        if (s <= 0) continue;
        decoded++; hz = info.hz; ch = info.channels; pairs += (unsigned)s;
        int cnt = s * info.channels;
        mp3dec_f32_to_s16(pcmf, pcm16, cnt);
        for (int i = 0; i < cnt; i++) {
            double a = fabs((double)pcmf[i]);
            if (a > peak) peak = a;
            if (a > 1.0) clip_f++;
            if (pcm16[i] == 32767 || pcm16[i] == -32768) rail16++;
        }
        if (fp) fwrite(pcm16, sizeof(int16_t), (size_t)cnt, fp);
        if (ff) fwrite(pcmf, sizeof(float), (size_t)cnt, ff);
    }
    if (fp) fclose(fp);
    if (ff) fclose(ff);
    printf("{\"frames\":%ld,\"decoded\":%ld,\"hz\":%d,\"channels\":%d,\"pairs\":%llu,\"peak\":%.9f,\"float_over_1\":%llu,\"rail16\":%llu}\n",
           frames, decoded, hz, ch, pairs, peak, clip_f, rail16);
    free(buf);
    return 0;
}
