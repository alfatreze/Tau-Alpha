/* MP3 v2 float oracle: a command-line decoder around the PRISTINE lieff/minimp3 (CC0 1.0, third_party/minimp3/minimp3.h, unchanged).
 * Own code (Tau Alpha, MIT). Decodes a whole MP3 file to 16-bit PCM and/or a float dump, deterministically:
 *   oracle <in.mp3> [--pcm out.s16le] [--f32 out.f32le] [--frames N] [--verbose]
 * Always prints one JSON line to stdout: frames, decoded frames, hz, channels, sample pairs, float peak (1.0 = 16-bit full scale), count of
 * samples beyond +-1.0 (clipping potential of the float output), count of rail samples in the 16-bit output.
 * Build (tools/mp3_v2/oracle.py does it): cc -O2 -ffp-contract=off -DMINIMP3_NO_SIMD -DMINIMP3_ONLY_MP3 -DMINIMP3_FLOAT_OUTPUT ...
 *   -ffp-contract=off so an FMA-capable host (arm64) cannot change the float results: output is bit-identical run to run and host to host.
 * The 16-bit output is mp3dec_f32_to_s16 of the float output (minimp3's own conversion, scalar path), which equals the library's integer
 * output (cross-checked once against a build without MINIMP3_FLOAT_OUTPUT, result in the WP-01 report). */
/* Stage dump (WP-06): built only with -DORACLE_STAGE_DUMP against an INSTRUMENTED COPY of minimp3.h that oracle.py writes into build/mp3_v2/stage/ (three hook calls
 * inserted at anchors that must each occur exactly once; third_party/minimp3/ is never edited). The hooks only copy data out; the decode is unchanged (the test
 * checks the stage build's PCM against the plain build byte for byte). Record format: see the comment above g_stage and docs/features/MP3_V2_WP06_REFERENCE_MODEL_2026-11-25.md. */
#ifdef ORACLE_STAGE_DUMP
#include <stdint.h>
void oracle_hook_spectrum(const float *grbuf, const uint8_t *sfbtab, int n_long_sfb, int n_short_sfb, int block_type, int n_long_bands, const float *overlap, int ch);
void oracle_hook_subband(const float *grbuf, int ch);
void oracle_hook_granule(const float *qmf_state, int nch);
#define ORACLE_HOOK_SPECTRUM(...) oracle_hook_spectrum(__VA_ARGS__)
#define ORACLE_HOOK_SUBBAND(...) oracle_hook_subband(__VA_ARGS__)
#define ORACLE_HOOK_GRANULE(...) oracle_hook_granule(__VA_ARGS__)
#endif
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ORACLE_STAGE_DUMP
/* Stage file, little endian, one record per decoded granule (576 sample pairs):
 *   u32 magic 0x4C4E5247 ("GRNL"), u32 nch (1 or 2), u32 qmf_zero (the polyphase history was all zero before this granule),
 *   then per channel: u32 block_type (0 normal, 1 start, 2 short, 3 stop), u32 n_long_bands (subbands coded long: 0 or the mixed-block count), u32 n_long_sfb, u32 n_short_sfb,
 *   u32 overlap_zero (the IMDCT overlap of this channel was all zero before the granule), u8 sfb[40] (the band-width row of the granule as minimp3 holds it: n_long_sfb
 *   long widths, then n_short_sfb short widths, each repeated for the three windows, zero terminated), float spectrum[576] (requantised, after stereo processing,
 *   BEFORE reordering/alias reduction: long part sequential, short part per band as window 0 / window 1 / window 2 runs),
 *   float subband[576] (after alias reduction, IMDCT, overlap-add and frequency inversion: [subband 0..31][sample 0..17]).
 * Scale: both float arrays are minimp3's own units (the model divides by a power of two, see the report). */
static FILE *g_stage;
static struct { uint32_t bt, nlb, nls, nss, ovz; uint8_t sfb[40]; float spec[576], sub[576]; } g_ch[2];
void oracle_hook_spectrum(const float *grbuf, const uint8_t *sfbtab, int n_long_sfb, int n_short_sfb, int block_type, int n_long_bands, const float *overlap, int ch)
{
    int i, z = 1;
    g_ch[ch].bt = (uint32_t)block_type; g_ch[ch].nlb = (uint32_t)n_long_bands; g_ch[ch].nls = (uint32_t)n_long_sfb; g_ch[ch].nss = (uint32_t)n_short_sfb;
    memset(g_ch[ch].sfb, 0, 40);
    for (i = 0; i < 40 && i < n_long_sfb + n_short_sfb + 1; i++) g_ch[ch].sfb[i] = sfbtab[i];
    for (i = 0; i < 9*32; i++) if (overlap[i] != 0.0f) { z = 0; break; }
    g_ch[ch].ovz = (uint32_t)z;
    memcpy(g_ch[ch].spec, grbuf, 576*sizeof(float));
}
void oracle_hook_subband(const float *grbuf, int ch) { memcpy(g_ch[ch].sub, grbuf, 576*sizeof(float)); }
void oracle_hook_granule(const float *qmf_state, int nch)
{
    uint32_t h[3] = { 0x4C4E5247u, (uint32_t)nch, 1 };
    int i, c;
    for (i = 0; i < 15*2*32; i++) if (qmf_state[i] != 0.0f) { h[2] = 0; break; }
    if (!g_stage) return;
    fwrite(h, 4, 3, g_stage);
    for (c = 0; c < nch; c++) {
        uint32_t m[5] = { g_ch[c].bt, g_ch[c].nlb, g_ch[c].nls, g_ch[c].nss, g_ch[c].ovz };
        fwrite(m, 4, 5, g_stage); fwrite(g_ch[c].sfb, 1, 40, g_stage);
        fwrite(g_ch[c].spec, sizeof(float), 576, g_stage); fwrite(g_ch[c].sub, sizeof(float), 576, g_stage);
    }
}
#endif

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
    const char *in = NULL, *pcm_path = NULL, *f32_path = NULL, *stage_path = NULL;
    long max_frames = -1; int verbose = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pcm") && i + 1 < argc) pcm_path = argv[++i];
        else if (!strcmp(argv[i], "--f32") && i + 1 < argc) f32_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--stage") && i + 1 < argc) stage_path = argv[++i];
        else if (!in) in = argv[i];
        else { fprintf(stderr, "usage: oracle in.mp3 [--pcm out] [--f32 out] [--frames N]\n"); return 2; }
    }
    if (!in) { fprintf(stderr, "usage: oracle in.mp3 [--pcm out] [--f32 out] [--frames N]\n"); return 2; }
#ifdef ORACLE_STAGE_DUMP
    if (stage_path && !(g_stage = fopen(stage_path, "wb"))) { fprintf(stderr, "cannot open %s\n", stage_path); return 2; }
#else
    if (stage_path) { fprintf(stderr, "--stage needs the stage build (oracle.py build_stage)\n"); return 2; }
#endif
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
    if (left >= 128 && !memcmp(p + left - 128, "TAG", 3)) left -= 128;   /* trailing ID3v1, then APEv2 (footer "APETAGEX"): not audio, as minimp3_ex strips them */
    if (left >= 32 && !memcmp(p + left - 32, "APETAGEX", 8)) {
        uint32_t sz = (uint32_t)p[left - 20] | ((uint32_t)p[left - 19] << 8) | ((uint32_t)p[left - 18] << 16) | ((uint32_t)p[left - 17] << 24);
        uint32_t fl = (uint32_t)p[left - 12] | ((uint32_t)p[left - 11] << 8) | ((uint32_t)p[left - 10] << 16) | ((uint32_t)p[left - 9] << 24);
        size_t cut = (size_t)sz + ((fl & 0x80000000u) ? 32 : 0);
        left = cut <= left ? left - cut : 0;
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
        if (verbose) fprintf(stderr, "frame %ld bytes %d offset %d samples %d\n", frames, info.frame_bytes, info.frame_offset, s);
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
#ifdef ORACLE_STAGE_DUMP
    if (g_stage) fclose(g_stage);
#endif
    printf("{\"frames\":%ld,\"decoded\":%ld,\"hz\":%d,\"channels\":%d,\"pairs\":%llu,\"peak\":%.9f,\"float_over_1\":%llu,\"rail16\":%llu}\n",
           frames, decoded, hz, ch, pairs, peak, clip_f, rail16);
    free(buf);
    return 0;
}
