/* Golden model of the (not yet built) FLAC LPC reconstruction unit (docs/research/FLAC_LPC_KERNEL_DESIGN.md,
 * B-364..B-366). Unlike the MP3 window unit's model (sim/mp3_poly_model.c, diffed against Helix's REAL
 * PolyphaseStereo with a fixed coefficient ROM), FLAC's coefficients are per-frame and streamed, not baked
 * into hardware -- there is no "real decoder" to link against here the way FDCT32/PolyphaseStereo are
 * linked in the MP3 case. This model IS the reference: the exact sequential state machine the RTL will
 * implement (one tap at a time into a 45-bit signed accumulator -- B-365's own proven-sufficient width,
 * not the 64-bit this project's own design doc assumed before that check existed), diffed here against an
 * "ideal" unbounded (int64_t, proven wide enough by the same B-365 check) computation of fw/flac.c's own
 * arithmetic: acc = sum(coef[j] * hist[j]) for j in 0..order-1, then one arithmetic right-shift.
 *
 * Test methodology mirrors sim/test_flac_lpc_symmetry.py exactly (synthetic random over FLAC's real legal
 * parameter bounds -- order 1-32, |coef| < 2^14, |sample| < 2^24 -- plus explicit worst-case corners) so
 * the C and Python models are independently checking the SAME claim; running both is what gives this
 * project's own standing "prove it twice, differently" confidence before trusting RTL against either alone.
 *
 * Also writes RTL testbench vectors to the path given on the command line: one 32-bit hex word per line
 * (matching sim/mp3_poly_model.c's own format), 67 words per vector -- order, shift, 32 coefficient slots
 * (zero-padded past `order`), 32 history slots (same padding), expected output -- fixed width so
 * sim/tb_tau_flac_lpc.v can $readmemh a flat array at a constant stride, unlike a variable-length line.
 *
 * Usage: flac_lpc_model <vectors.txt>   Exit 1 on any mismatch. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FLAC_MAX_ORDER   32
#define COEF_MAX_PREC    15                 /* fw/flac.c rejects prec == 16 */
#define SAMPLE_MAX_BITS  25                 /* 24-bit audio (fw/flac.c bps check) + 1 for the side channel */
#define ACC_BITS         45                 /* B-365: proven sufficient, both exhaustively and on 15.7M real samples */

static uint32_t rng = 20260928u;
static uint32_t rnd32(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static int32_t rnd_range(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd32() % (uint32_t)(hi - lo + 1)); }

/* fw/flac.c's own arithmetic, unbounded: int64_t is proven wide enough for any legal input (B-365), so
 * this line IS the trusted reference, not an approximation of one. */
static void ideal_predict(const int32_t *coef, const int32_t *hist, uint32_t order, int32_t shift,
                           int64_t *acc_out, int32_t *out)
{
    int64_t acc = 0;
    for (uint32_t j = 0; j < order; j++) acc += (int64_t)coef[j] * (int64_t)hist[j];
    *acc_out = acc;
    *out = (int32_t)(acc >> shift);
}

/* The RTL's own shape: one tap at a time into a fixed ACC_BITS-wide signed two's-complement register.
 * Returns 0 (leaving the outputs untouched) if any intermediate step actually overflows the register --
 * checked at every step, matching sim/test_flac_lpc_symmetry.py's own hardware_predict(). */
static int hw_predict(const int32_t *coef, const int32_t *hist, uint32_t order, int32_t shift,
                       int64_t *acc_out, int32_t *out)
{
    const int64_t lo = -((int64_t)1 << (ACC_BITS - 1));
    const int64_t hi = ((int64_t)1 << (ACC_BITS - 1)) - 1;
    int64_t acc = 0;
    for (uint32_t j = 0; j < order; j++) {
        acc += (int64_t)coef[j] * (int64_t)hist[j];
        if (acc < lo || acc > hi) return 0;
    }
    *acc_out = acc;
    *out = (int32_t)(acc >> shift);
    return 1;
}

static void rand_case(int32_t *coef, int32_t *hist, uint32_t order, int32_t *shift, int extreme)
{
    *shift = rnd_range(0, 31);
    int32_t cmax = (1 << (COEF_MAX_PREC - 1));
    int32_t smax = (1 << (SAMPLE_MAX_BITS - 1));
    for (uint32_t j = 0; j < order; j++) {
        if (extreme) {
            coef[j] = (rnd32() & 1) ? cmax - 1 : -cmax;
            hist[j] = (rnd32() & 1) ? smax - 1 : -smax;
        } else {
            coef[j] = rnd_range(-cmax, cmax - 1);
            hist[j] = rnd_range(-smax, smax - 1);
        }
    }
}

int main(int argc, char **argv)
{
    FILE *vf = argc > 1 ? fopen(argv[1], "w") : NULL;
    int32_t coef[FLAC_MAX_ORDER], hist[FLAC_MAX_ORDER];
    int mismatches = 0, overflows = 0, vectors = 0;

    /* random legal-range trials */
    for (int t = 0; t < 20000; t++) {
        uint32_t order = (uint32_t)rnd_range(1, FLAC_MAX_ORDER);
        int32_t shift;
        rand_case(coef, hist, order, &shift, 0);
        int64_t sw_acc, hw_acc; int32_t sw_out, hw_out;
        ideal_predict(coef, hist, order, shift, &sw_acc, &sw_out);
        if (!hw_predict(coef, hist, order, shift, &hw_acc, &hw_out)) { overflows++; continue; }
        if (hw_acc != sw_acc || hw_out != sw_out) { mismatches++; continue; }
        if (vf) {
            /* Fixed-width, one hex word per line (matches sim/mp3_poly_model.c's own vector format, so
             * the RTL testbench can $readmemh a flat array at a fixed stride): order, shift, 32
             * coefficient slots (zero-padded past `order`, MAX_ORDER is fixed so the testbench doesn't
             * need to parse a variable-length line), 32 history slots (same padding), expected output.
             * 67 words per vector. */
            fprintf(vf, "%08x\n%08x\n", order, (uint32_t)shift);
            for (uint32_t j = 0; j < FLAC_MAX_ORDER; j++) fprintf(vf, "%08x\n", (uint32_t)(j < order ? coef[j] : 0));
            for (uint32_t j = 0; j < FLAC_MAX_ORDER; j++) fprintf(vf, "%08x\n", (uint32_t)(j < order ? hist[j] : 0));
            fprintf(vf, "%08x\n", (uint32_t)hw_out);
            vectors++;
        }
    }

    /* explicit worst-case corners: every combination of order in {1,2,32}, shift in {0,15,31}, and
     * max-positive / max-negative / alternating-sign coefficient and sample patterns */
    static const uint32_t orders[] = { 1u, 2u, FLAC_MAX_ORDER };
    static const int32_t shifts[] = { 0, 15, 31 };
    for (unsigned oi = 0; oi < sizeof(orders) / sizeof(orders[0]); oi++)
        for (unsigned si = 0; si < sizeof(shifts) / sizeof(shifts[0]); si++)
            for (int kind = 0; kind < 3; kind++) {
                uint32_t order = orders[oi]; int32_t shift = shifts[si];
                int32_t cmax = (1 << (COEF_MAX_PREC - 1)) - 1, smax = (1 << (SAMPLE_MAX_BITS - 1)) - 1;
                for (uint32_t j = 0; j < order; j++) {
                    if (kind == 0)      { coef[j] = cmax;      hist[j] = smax; }
                    else if (kind == 1) { coef[j] = -cmax - 1; hist[j] = -smax - 1; }
                    else                { coef[j] = (j % 2 == 0) ? cmax : -cmax - 1;
                                          hist[j] = (j % 2 == 0) ? smax : -smax - 1; }
                }
                int64_t sw_acc, hw_acc; int32_t sw_out, hw_out;
                ideal_predict(coef, hist, order, shift, &sw_acc, &sw_out);
                if (!hw_predict(coef, hist, order, shift, &hw_acc, &hw_out)) { overflows++; continue; }
                if (hw_acc != sw_acc || hw_out != sw_out) { mismatches++; continue; }
            }

    printf("random legal-range trials: 20000, worst-case corners: %zu\n",
           sizeof(orders) / sizeof(orders[0]) * sizeof(shifts) / sizeof(shifts[0]) * 3u);
    printf("mismatches %d, overflows %d, RTL vectors written %d\n", mismatches, overflows, vectors);
    if (vf) fclose(vf);
    return (mismatches || overflows) ? 1 : 0;
}
