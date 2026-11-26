/* MP3 v2 WP-04: the Helix side of the rail and headroom count. Own code (Tau Alpha, MIT); it links Helix (the tree every consumer compiles: pristine upstream
 * + tau.patch, tools/helix_tree.py) built AS PLAYER 2 BUILDS IT (TAU_POLY_FW=1, TAU_POLY_REQUIRED=1: no software window), with the window unit replaced by
 * its golden model (sim/mp3_poly_model_core.h, proven equal to the RTL by WP-02), one model instance per requested gain.
 *
 *   rail_count in.mp3 --ref oracle.f32le [--gains 1,0.7079,...]
 *
 * Instance 0 is the shipped decoder (gain 1: the words pushed to the unit are Helix's). Instance k>0 is "a pre-synthesis gain": every word of the 64 of
 * a slot is multiplied by g_k and rounded to nearest BEFORE the window, which is exactly where a decoder pre-gain would sit (the window is linear).
 * The float oracle dump (tools/mp3_v2/oracle.py --f32: pristine minimp3, 1.0 = 16-bit full scale) is the reference; the two decoders are aligned by a
 * whole-sample lag found on the first loud samples (reported; 0 expected when both skip the same frames).
 *
 * Output: one JSON line. Per instance: Helix samples, samples at the 16-bit rails, peak, error against the ideal clamp16(oracle * g) (count of |err| > 1 LSB,
 * max |err|, sum of squares; also the count of |err| > 2 LSB: Helix and the float oracle differ by at most 2 LSB, so more than 2 can only be damage) and the same for the POST-clamp alternative (Helix instance 0 clamped to 16 bits, THEN multiplied by g).
 * Instance 0 also carries the rail classification (both at a rail / Helix only / oracle only) and the largest word pushed to the unit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "mp3dec.h"
#include "mp3_poly_hw.h"
#include TABLES_H

#define NINST 7
#define K 0
#include "rail_unit_inst.h"
#undef K
#define K 1
#include "rail_unit_inst.h"
#undef K
#define K 2
#include "rail_unit_inst.h"
#undef K
#define K 3
#include "rail_unit_inst.h"
#undef K
#define K 4
#include "rail_unit_inst.h"
#undef K
#define K 5
#include "rail_unit_inst.h"
#undef K
#define K 6
#include "rail_unit_inst.h"
#undef K

static void u_clear(int k)
{
    switch (k) {
#define CL(n) case n: memset(ring_##n, 0, sizeof ring_##n); head_##n[0] = head_##n[1] = 0; break;
    CL(0) CL(1) CL(2) CL(3) CL(4) CL(5) CL(6)
#undef CL
    }
}
static void u_push(int k, int ch, const int *w)
{
    switch (k) {
#define PU(n) case n: push_##n(ch, w); break;
    PU(0) PU(1) PU(2) PU(3) PU(4) PU(5) PU(6)
#undef PU
    }
}
static void u_window(int k, int ch, short *out)
{
    switch (k) {
#define WI(n) case n: window_##n(ch, out); break;
    WI(0) WI(1) WI(2) WI(3) WI(4) WI(5) WI(6)
#undef WI
    }
}

/* ---- the unit stub (same API as fw2/decode/poly_hw.c; TAU_POLY_REQUIRED never calls the self-check) ---- */
int tau_poly_hw_enable = 1;
int tau_poly_verify_left;
unsigned tau_poly_stat_slots, tau_poly_stat_mismatch, tau_poly_stat_timeout, tau_poly_stat_silent;
int tau_poly_wlog[33];
int tau_poly_wn;

static int ninst = 1;
static double gain[NINST] = {1.0};
static short staged[NINST][64];
static int pending, pend_mono;
static int64_t maxw;                                   /* largest |word| pushed (instance 0, unscaled) */
static unsigned long long slots;
static short *outbuf[NINST]; static size_t outn[NINST], outcap[NINST];

static void out_append(int k, const short *s, int n)
{
    if (outn[k] + (size_t)n > outcap[k]) { outcap[k] = outcap[k] ? outcap[k] * 2 : (1u << 20); while (outn[k] + (size_t)n > outcap[k]) outcap[k] *= 2; outbuf[k] = realloc(outbuf[k], outcap[k] * sizeof(short)); if (!outbuf[k]) exit(3); }
    memcpy(outbuf[k] + outn[k], s, (size_t)n * sizeof(short)); outn[k] += (size_t)n;
}
void tau_poly_hw_clear(void) { for (int k = 0; k < ninst; k++) u_clear(k); tau_poly_verify_left = 0; }
int tau_poly_hw_start(const int *w0, const int *w1)
{
    if (pending) { fprintf(stderr, "PROTOCOL: start while a slot is in flight\n"); exit(2); }
    pend_mono = (w0 == w1);                            /* Subband() pushes a mono slot as (w0, w0) */
    for (int i = 0; i < 32; i++) { int64_t a = w0[i] < 0 ? -(int64_t)w0[i] : w0[i]; if (a > maxw) maxw = a; if (!pend_mono) { a = w1[i] < 0 ? -(int64_t)w1[i] : w1[i]; if (a > maxw) maxw = a; } }
    for (int k = 0; k < ninst; k++) {
        int a[32], b[32];
        for (int i = 0; i < 32; i++) {
            if (gain[k] == 1.0) { a[i] = w0[i]; b[i] = w1[i]; }
            else {                                       /* round to nearest, saturate at the signed 32-bit word */
                double x = llround((double)w0[i] * gain[k]), y = llround((double)w1[i] * gain[k]);
                a[i] = x > 2147483647.0 ? 2147483647 : x < -2147483648.0 ? INT32_MIN : (int)x;
                b[i] = y > 2147483647.0 ? 2147483647 : y < -2147483648.0 ? INT32_MIN : (int)y;
            }
        }
        u_push(k, 0, a); u_push(k, 1, b);
        short l[32], r[32];
        u_window(k, 0, l); u_window(k, 1, r);
        for (int i = 0; i < 32; i++) { staged[k][2 * i] = l[i]; staged[k][2 * i + 1] = r[i]; }
    }
    pending = 1;
    return 1;
}
int tau_poly_hw_finish(short *pcm)
{
    if (!pending) { fprintf(stderr, "PROTOCOL: finish with nothing in flight\n"); exit(2); }
    pending = 0;
    memcpy(pcm, staged[0], sizeof staged[0]);
    for (int k = 1; k < ninst; k++) {
        if (pend_mono) { short m[32]; for (int i = 0; i < 32; i++) m[i] = staged[k][2 * i]; out_append(k, m, 32); }
        else out_append(k, staged[k], 64);
    }
    slots++; tau_poly_stat_slots++;
    return 1;
}
int tau_poly_hw_slot(const int *w0, const int *w1, short *pcm) { return tau_poly_hw_start(w0, w1) && tau_poly_hw_finish(pcm); }

/* ---- minimp3's 16-bit conversion (scalar path), the oracle's own: sample in 16-bit units ---- */
static int16_t s16(float s)
{
    if (s >= 32766.5f) return 32767;
    if (s <= -32767.5f) return -32768;
    int16_t v = (int16_t)(s + .5f);
    v -= (v < 0);
    return v;
}

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


/* ---- integrated loudness, ITU-R BS.1770 K-weighting and gating, written from the standard's filter prototypes (shelf 1681.97 Hz +4.0 dB, high-pass 38.14 Hz;
 * the bilinear forms below reproduce the standard's 48 kHz coefficients) ---- */
typedef struct { double b[3], a[3]; double z1, z2; } bq_t;
static double bq_run(bq_t *f, double x) { double y = f->b[0] * x + f->z1; f->z1 = f->b[1] * x - f->a[1] * y + f->z2; f->z2 = f->b[2] * x - f->a[2] * y; return y; }
static void k_filters(double fs, bq_t *sh, bq_t *hp)
{
    const double PI = 3.14159265358979323846;
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = tan(PI * f0 / fs), Vh = pow(10.0, G / 20.0), Vb = pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
    memset(sh, 0, sizeof *sh); memset(hp, 0, sizeof *hp);
    sh->b[0] = (Vh + Vb * K / Q + K * K) / a0; sh->b[1] = 2 * (K * K - Vh) / a0; sh->b[2] = (Vh - Vb * K / Q + K * K) / a0;
    sh->a[0] = 1; sh->a[1] = 2 * (K * K - 1) / a0; sh->a[2] = (1 - K / Q + K * K) / a0;
    f0 = 38.13547087602444; Q = 0.5003270373238773; K = tan(PI * f0 / fs); a0 = 1 + K / Q + K * K;
    hp->b[0] = 1; hp->b[1] = -2; hp->b[2] = 1; hp->a[0] = 1; hp->a[1] = 2 * (K * K - 1) / a0; hp->a[2] = (1 - K / Q + K * K) / a0;
}
/* LUFS of interleaved float samples (1.0 = full scale), channels weighted 1 (mono: the one channel); -70 LUFS when nothing passes the gates */
static double lufs(const float *x, size_t nsamp, int ch, int hz)
{
    size_t frames = nsamp / (size_t)ch, seg = (size_t)(hz / 10), nseg = frames / seg;
    if (nseg < 4) return -70.0;
    double *e = calloc(nseg, sizeof(double));
    for (int c = 0; c < ch; c++) {
        bq_t sh, hp; k_filters((double)hz, &sh, &hp);
        for (size_t i = 0; i < nseg * seg; i++) { double v = bq_run(&hp, bq_run(&sh, (double)x[i * (size_t)ch + (size_t)c])); e[i / seg] += v * v; }
    }
    size_t nb = nseg - 3; double *bl = malloc(nb * sizeof(double)); double sum = 0; size_t cnt = 0;
    for (size_t b = 0; b < nb; b++) {
        bl[b] = (e[b] + e[b + 1] + e[b + 2] + e[b + 3]) / (4.0 * (double)seg);   /* sum over channels of mean square */
        if (-0.691 + 10 * log10(bl[b] + 1e-30) > -70.0) { sum += bl[b]; cnt++; }
    }
    double r = -70.0;
    if (cnt) {
        double rel = -0.691 + 10 * log10(sum / (double)cnt) - 10.0, s2 = 0; size_t c2 = 0;
        for (size_t b = 0; b < nb; b++) if (-0.691 + 10 * log10(bl[b] + 1e-30) > rel && -0.691 + 10 * log10(bl[b] + 1e-30) > -70.0) { s2 += bl[b]; c2++; }
        if (c2) r = -0.691 + 10 * log10(s2 / (double)c2);
    }
    free(e); free(bl);
    return r;
}

typedef struct { unsigned long long rails, n_gt1, n_gt2, post_gt1, post_gt2, post_rails; double max_err, sq, post_max, post_sq; int peak; } inst_t;

int main(int argc, char **argv)
{
    const char *in = NULL, *ref = NULL, *gains = NULL, *dump0 = NULL, *rgspec = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ref") && i + 1 < argc) ref = argv[++i];
        else if (!strcmp(argv[i], "--gains") && i + 1 < argc) gains = argv[++i];
        else if (!strcmp(argv[i], "--dump0") && i + 1 < argc) dump0 = argv[++i];
        else if (!strcmp(argv[i], "--rg") && i + 1 < argc) rgspec = argv[++i];
        else if (!in) in = argv[i];
        else { fprintf(stderr, "usage: rail_count in.mp3 --ref oracle.f32 [--gains 1,g1,..]\n"); return 2; }
    }
    if (!in || !ref) { fprintf(stderr, "usage: rail_count in.mp3 --ref oracle.f32 [--gains 1,g1,..]\n"); return 2; }
    if (gains) {
        char *s = strdup(gains), *t = strtok(s, ","); ninst = 0;
        while (t && ninst < NINST) { gain[ninst++] = atof(t); t = strtok(NULL, ","); }
        if (ninst < 1 || gain[0] != 1.0) { fprintf(stderr, "the first gain must be 1\n"); return 2; }
    }
    size_t n; uint8_t *buf = slurp(in, &n);
    if (!buf) { fprintf(stderr, "cannot read %s\n", in); return 2; }
    size_t rn; uint8_t *rb = slurp(ref, &rn);
    if (!rb) { fprintf(stderr, "cannot read %s\n", ref); return 2; }
    const float *of = (const float *)rb; size_t no = rn / sizeof(float);

    /* --rg HZ,CH: the LAST gain becomes the estimated ReplayGain 2.0 track gain, min(-18 - LUFS, 0 dB), from the oracle's float output */
    double lufs_v = 0; int have_lufs = 0;
    if (rgspec) {
        int hz = 0, c = 0; if (sscanf(rgspec, "%d,%d", &hz, &c) != 2 || hz <= 0 || c < 1 || ninst < 2) { fprintf(stderr, "bad --rg\n"); return 2; }
        lufs_v = lufs(of, no, c, hz); have_lufs = 1;
        double db = -18.0 - lufs_v; if (db > 0) db = 0;
        gain[ninst - 1] = pow(10.0, db / 20.0);
    }
    /* tags as the oracle strips them */
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

    /* Helix, the way the firmware loop does (sync search + MP3Decode per frame) */
    HMP3Decoder dec = MP3InitDecoder(); if (!dec) return 3;
    static short pcm[1152 * 2];
    long frames = 0, errs = 0; int ch = 0, rate = 0;
    size_t pos = 0;
    while (pos < left) {
        int off = MP3FindSyncWord((unsigned char *)p + pos, (int)(left - pos));
        if (off < 0) break;
        pos += (size_t)off;
        unsigned char *ip = (unsigned char *)p + pos; int rem = (int)(left - pos);
        int err = MP3Decode(dec, &ip, &rem, pcm, 0);
        if (err) { errs++; if (err == ERR_MP3_INDATA_UNDERFLOW) break; pos += 1; continue; }
        pos = (size_t)(ip - p);
        MP3FrameInfo fi; MP3GetLastFrameInfo(dec, &fi);
        ch = fi.nChans; rate = fi.samprate;
        out_append(0, pcm, fi.outputSamps);
        frames++;
    }
    size_t n0 = outn[0];
    int chn = ch ? ch : 1;
    if (dump0) { FILE *d = fopen(dump0, "wb"); if (d) { fwrite(outbuf[0], 2, n0, d); fclose(d); } }

    /* alignment: lag such that Helix[i + lag] corresponds to oracle[i] */
    long lag = 0;
    if (n0 != no) {
        size_t i0 = 0;
        while (i0 < no && fabsf(of[i0]) * 32768.0f < 2000.0f) i0++;
        double best = 1e300; long bl = 0;
        for (long l = -4608; l <= 4608; l++) {
            double e = 0; long cnt = 0;
            for (size_t i = i0; i < i0 + 20000 && i < no; i++) {
                long j = (long)i + l; if (j < 0 || (size_t)j >= n0) { e += 65536; continue; }
                e += fabs((double)outbuf[0][j] - (double)s16(of[i] * 32768.0f)); cnt++;
            }
            if (e < best) { best = e; bl = l; }
        }
        lag = bl;
    }

    inst_t it[NINST]; memset(it, 0, sizeof it);
    unsigned long long both = 0, h_only = 0, h_only_clear = 0, o_only = 0, o_only_far = 0, o_over1 = 0, unmatched = 0, align_bad = 0, matched = 0;
    float opeak = 0;
    for (size_t i = 0; i < no; i++) {
        long j = (long)i + lag;
        float o16 = of[i] * 32768.0f;
        if (fabsf(of[i]) > opeak) opeak = fabsf(of[i]);
        if (fabsf(of[i]) > 1.0f) o_over1++;
        if (j < 0 || (size_t)j >= n0) { unmatched++; continue; }
        matched++;
        int h0 = outbuf[0][j];
        int hr = (h0 == 32767 || h0 == -32768);
        int i0v = s16(o16); int orl = (i0v == 32767 || i0v == -32768);
        if (hr && orl) both++;
        else if (hr) { h_only++; if (o16 < 32766.5f - 2.0f && o16 > -32767.5f + 2.0f) h_only_clear++; }
        else if (orl) { o_only++; if (o16 >= 32766.5f + 2.0f || o16 <= -32767.5f - 2.0f) o_only_far++; }
        if (abs(h0 - i0v) > 2) align_bad++;
        for (int k = 0; k < ninst; k++) {
            int hk = (k == 0) ? h0 : ((size_t)j < outn[k] ? outbuf[k][j] : 0);
            int ideal = s16(o16 * (float)gain[k]);
            double e = fabs((double)(hk - ideal));
            if (e > 1.0) it[k].n_gt1++;
            if (e > 2.0) it[k].n_gt2++;
            if (e > it[k].max_err) it[k].max_err = e;
            it[k].sq += e * e;
            if (k > 0) {
                int q = s16((float)h0 * (float)gain[k]);               /* gain AFTER the clamp */
                double pe = fabs((double)(q - ideal));
                if (pe > 1.0) it[k].post_gt1++;
                if (pe > 2.0) it[k].post_gt2++;
                if (pe > it[k].post_max) it[k].post_max = pe;
                it[k].post_sq += pe * pe;
                if (q == 32767 || q == -32768) it[k].post_rails++;
            }
        }
    }
    for (int k = 0; k < ninst; k++) {
        for (size_t j = 0; j < outn[k]; j++) {
            int v = outbuf[k][j];
            if (v == 32767 || v == -32768) it[k].rails++;
            int a = v < 0 ? -v : v; if (a > it[k].peak) it[k].peak = a;
        }
    }
    printf("{\"helix_frames\":%ld,\"helix_errors\":%ld,\"hz\":%d,\"channels\":%d,\"helix_samples\":%zu,\"oracle_samples\":%zu,\"lag\":%ld,\"matched\":%llu,\"unmatched\":%llu,"
           "\"align_bad\":%llu,\"oracle_peak\":%.9f,\"oracle_over_1\":%llu,\"both_rail\":%llu,\"helix_only_rail\":%llu,\"helix_only_rail_clear\":%llu,"
           "\"oracle_only_rail\":%llu,\"oracle_only_rail_far\":%llu,\"slots\":%llu,\"max_word\":%lld,\"lufs\":%.4f,\"instances\":[",
           frames, errs, rate, chn, n0, no, lag, matched, unmatched, align_bad, opeak, o_over1, both, h_only, h_only_clear, o_only, o_only_far, slots, (long long)maxw, have_lufs ? lufs_v : 0.0);
    for (int k = 0; k < ninst; k++)
        printf("%s{\"gain\":%.9f,\"samples\":%zu,\"rails\":%llu,\"peak\":%d,\"err_gt1\":%llu,\"err_gt2\":%llu,\"err_max\":%.0f,\"err_sq\":%.0f,\"post_err_gt1\":%llu,\"post_err_gt2\":%llu,\"post_err_max\":%.0f,\"post_err_sq\":%.0f,\"post_rails\":%llu}",
               k ? "," : "", gain[k], outn[k], it[k].rails, it[k].peak, it[k].n_gt1, it[k].n_gt2, it[k].max_err, it[k].sq, it[k].post_gt1, it[k].post_gt2, it[k].post_max, it[k].post_sq, it[k].post_rails);
    printf("]}\n");
    return 0;
}
