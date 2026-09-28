/* B-370 host redirect check (docs/research/FLAC_LPC_KERNEL_DESIGN.md section 7 item 4): drives the REAL
 * fw/flac.c LPC subframe code (compiled with -DFLAC_TEST_EXPOSE so its static entrypoints are reachable),
 * once as shipped (TAU_LPC_FW=0) and once with the hardware redirect active (TAU_LPC_FW=1, this file's
 * own tau_lpc_hw_* stub). The stub does the IDENTICAL math flac.c's own software loop already does --
 * this harness is not re-proving the arithmetic (sim/test_flac_lpc_symmetry.py and tau_flac_lpc.sv's own
 * testbench, B-365/B-368, already did that two independent ways); it proves the GLUE fw/flac.c's redirect
 * adds: warm-up reversal into the hardware's most-recent-first convention, the residual/out[] handoff,
 * and -- via the optional fail-at argument -- the mid-subframe software-completion fallback, using a real
 * subframe()/subframe_stream() call rather than a hand-simulated one.
 *
 * Built twice by sim/test_flac_lpc_fw_redirect.py (once per TAU_LPC_FW value); the outputs of all
 * variants (subframe/stream x fw 0/1 x fail-at -1/mid-subframe) must be byte-for-byte identical.
 *
 * Usage: harness <blob> subframe|stream <order> <bps> <n> <fail_at|-1>
 * Prints one decimal reconstructed sample per line (n - order of them: the warm-up itself is not
 * printed, since it comes straight from the bitstream on every variant and proves nothing). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "flac.h"

extern flac_err flac_test_subframe(flac_t *f, int32_t *out, uint32_t bps);
extern flac_err flac_test_subframe_stream(flac_t *f, uint32_t bps, uint32_t out_bps,
                                           uint8_t m, flac_sink_fn sink, void *sctx);

static uint8_t *g_blob;
static long g_blob_len, g_blob_pos;

static int blob_read(void *ctx, uint8_t *dst, int n)
{
    (void)ctx;
    long avail = g_blob_len - g_blob_pos;
    if (avail <= 0) return 0;
    if (n > avail) n = (int)avail;
    memcpy(dst, g_blob + g_blob_pos, (size_t)n);
    g_blob_pos += n;
    return n;
}

#if TAU_LPC_FW
static long g_fail_at = -1;
static long g_sample_no;
int tau_lpc_hw_enable = 1;
unsigned tau_lpc_stat_samples, tau_lpc_stat_timeout;
static int32_t hw_order;
static int32_t hw_shift;
static int32_t hw_coef[32];
static int32_t hw_hist[32];   /* index 0 = most recent, same convention as the real MMIO glue */

int tau_lpc_hw_begin(uint32_t order, int32_t shift, const int32_t *coef, const int32_t *warm)
{
    if (!tau_lpc_hw_enable) return 0;
    hw_order = (int32_t)order;
    hw_shift = shift;
    for (uint32_t j = 0; j < order; j++) hw_coef[j] = coef[j];
    for (uint32_t j = 0; j < order; j++) hw_hist[j] = warm[order - 1u - j];
    return 1;
}

int32_t tau_lpc_hw_sample(int32_t residual, int *ok)
{
    if (g_fail_at >= 0 && g_sample_no++ == g_fail_at) {
        *ok = 0;
        tau_lpc_hw_enable = 0;
        return 0;
    }
    int64_t p = 0;
    for (int32_t j = 0; j < hw_order; j++) p += (int64_t)hw_coef[j] * (int64_t)hw_hist[j];
    int32_t s = (int32_t)(p >> hw_shift) + residual;
    for (int32_t j = hw_order - 1; j > 0; j--) hw_hist[j] = hw_hist[j - 1];
    if (hw_order) hw_hist[0] = s;
    tau_lpc_stat_samples++;
    *ok = 1;
    return s;
}
#endif

static int16_t g_stream_out[65536];
static uint32_t g_stream_n;
static void sink(void *ctx, const int16_t *pcm, uint32_t frames)
{
    (void)ctx;
    for (uint32_t i = 0; i < frames; i++) g_stream_out[g_stream_n++] = pcm[2u * i + 1u];   /* mode 0 (independent): r_ == S */
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: %s <blob> subframe|stream <order> <bps> <n> <fail_at>\n", argv[0]);
        return 2;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("open"); return 2; }
    fseek(fp, 0, SEEK_END); g_blob_len = ftell(fp); fseek(fp, 0, SEEK_SET);
    g_blob = malloc((size_t)g_blob_len);
    if (!g_blob || fread(g_blob, 1, (size_t)g_blob_len, fp) != (size_t)g_blob_len) {
        fprintf(stderr, "short read\n");
        return 2;
    }
    fclose(fp);

    uint32_t order = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t bps   = (uint32_t)strtoul(argv[4], NULL, 10);
    uint32_t n     = (uint32_t)strtoul(argv[5], NULL, 10);
#if TAU_LPC_FW
    g_fail_at = strtol(argv[6], NULL, 10);
#endif

    flac_t f;
    memset(&f, 0, sizeof f);
    f.read = blob_read;
    f.blocksize = n;

    if (!strcmp(argv[2], "subframe")) {
        int32_t *out = calloc(n, sizeof(int32_t));
        if (!out) return 2;
        flac_err e = flac_test_subframe(&f, out, bps);
        if (e) { fprintf(stderr, "subframe error %d\n", e); return 1; }
        for (uint32_t i = order; i < n; i++) printf("%d\n", out[i]);
    } else if (!strcmp(argv[2], "stream")) {
        int32_t *ch0 = calloc(n, sizeof(int32_t));
        if (!ch0) return 2;
        f.ch0 = ch0;
        f.ch0_cap = n;
        flac_err e = flac_test_subframe_stream(&f, bps, bps, 0u /* independent: EMIT's r_ == S */, sink, NULL);
        if (e) { fprintf(stderr, "subframe_stream error %d\n", e); return 1; }
        for (uint32_t i = order; i < g_stream_n; i++) printf("%d\n", g_stream_out[i]);
    } else {
        fprintf(stderr, "mode must be subframe or stream\n");
        return 2;
    }
    return 0;
}
