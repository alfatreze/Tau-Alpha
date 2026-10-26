/* Harness for the frame decoder in fw2/cores/flac2_core.h. Usage:
 *   harness <frames.bin> <streaminfo hex> <chunks csv> <slices csv> <outcaps csv> <flags> <pcm out> <stats out> [corrupt-stop]
 * The input is the frame data only (metadata already skipped by the test). The window handed to each call is the next chunk, grown while the decoder keeps asking for more (a real transport's
 * window grows as bytes arrive). Pairs are staged per frame and committed only when the frame ends with a good CRC-16 (spec 5.6). PCM out: int32 little endian L,R. Stats: one line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac2_core.h"
/* A model of the LPC unit (tau_flac_lpc.sv): history[0] is the most recent sample; sample() = residual + ((sum coef[j] * hist[j]) >> shift) in 64 bits, then push. Fails after m_fail_after samples. */
static int32_t m_coef[32], m_hist[32]; static uint32_t m_order; static int32_t m_shift; static uint64_t m_count, m_fail_after = ~0ull;
static int m_begin(uint32_t order, int32_t shift, const int32_t *coef, const int32_t *warm)
{ m_order = order; m_shift = shift; for (uint32_t j = 0; j < order; j++) { m_coef[j] = coef[j]; m_hist[j] = warm[order - 1u - j]; } return 1; }
static int32_t m_sample(int32_t res, int *ok)
{
    if (m_count++ >= m_fail_after) { *ok = 0; return 0; }
    int64_t acc = 0; for (uint32_t j = 0; j < m_order; j++) acc += (int64_t)m_coef[j] * m_hist[j];
    int32_t sm = (int32_t)((uint32_t)res + (uint32_t)(int32_t)(acc >> m_shift));
    for (uint32_t j = m_order - 1u; j > 0; j--) m_hist[j] = m_hist[j - 1u];
    m_hist[0] = sm; *ok = 1; return sm;
}
static const f2_hw_t model_hw = { m_begin, m_sample };
static uint32_t csv(const char *s, uint32_t *o, uint32_t max) { uint32_t n = 0; while (*s && n < max) { o[n++] = (uint32_t)strtoul(s, (char **)&s, 10); if (*s == ',') s++; } return n; }
static uint8_t *slurp(const char *path, uint32_t *n) { FILE *f = fopen(path, "rb"); if (!f) exit(3); fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc((size_t)l + 16); if (fread(b, 1, (size_t)l, f) != (size_t)l) exit(3); fclose(f); *n = (uint32_t)l; return b; }
int main(int argc, char **argv)
{
    if (argc < 9) return 2;
    uint32_t n, ch[64], sl[64], oc[64], nc = csv(argv[3], ch, 64), ns = csv(argv[4], sl, 64), no = csv(argv[5], oc, 64), flags = (uint32_t)atoi(argv[6]);
    uint8_t *data = slurp(argv[1], &n), si_b[34] = {0};
    for (int i = 0; i < 34; i++) { unsigned x; sscanf(argv[2] + 2 * i, "%2x", &x); si_b[i] = (uint8_t)x; }
    f2_info_t si; if (f2_streaminfo(si_b, &si) != F2_OK) return 4;
    static int32_t buf[65536], pend[2 * 65536], tmp[2 * 65536]; f2_dec_t *d = calloc(1, sizeof *d);
    f2_dec_init(d, &si, buf, 65536, flags);
    if (argc > 9 && argv[9][0] == 'h') { d->hw = &model_hw; if (argv[9][1]) m_fail_after = strtoull(argv[9] + 1, 0, 10); }
    FILE *po = fopen(argv[7], "wb"); uint32_t off = 0, ci = 0, si_i = 0, oi = 0, grow = 0, npend = 0, okf = 0, badf = 0, spins = 0, lasterr = 0; int fin = 0, status = 0;
    while (!fin && ++spins < 20000000u) {
        uint32_t len = ch[ci % nc] + grow; if (off + len > n) len = n - off; uint32_t cap = oc[oi % no], used = 0, np = 0;
        int st = f2_dec_step(d, data + off, len, tmp, cap, sl[si_i % ns], &used, &np);
        ci++; si_i++; oi++;
        if (npend + np <= 65536u) { memcpy(pend + 2 * npend, tmp, 8u * np); npend += np; } else { status = 9; break; }
        off += used;
        if (d->frame_done) {
            if (d->frame_ok) { fwrite(pend, 8, npend, po); okf++; } else badf++;
            npend = 0;
        }
        if (st == F2_ERR) { lasterr = d->err; status = 2; fin = 1; }
        else if (st == F2_NEED) { if (off >= n) { status = (d->phase == F2_PH_HDR && npend == 0) ? 0 : 1; fin = 1; } else grow = (used || np) ? 0u : len; }
        else grow = 0;
    }
    if (spins >= 20000000u) status = 8;
    fclose(po);
    FILE *so = fopen(argv[8], "w"); fprintf(so, "%d %u %u %u %u %u %u %u\n", status, okf, badf, lasterr, npend, off, d->hw_samples, d->hw_fails); fclose(so);
    return 0;
}
