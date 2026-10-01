/* Golden model of the (not yet fitted) Cymo polyphase FIR resampler (docs/features/CYMO_AUDIO_ENGINE.md
 * section 15/9, docs/AUDIT_TRAIL.md B-467+). Unlike the MP3 window unit (sim/mp3_poly_model.c, diffed
 * against Helix's real PolyphaseStereo) there is no existing reference decoder for this unit -- it is a
 * NEW design (160-bank, 32-tap, exact 44100:48000 = 160:147 polyphase FIR, Kaiser-windowed, following the
 * neoge/pocket-mp3 architecture reviewed in CYMO_AUDIO_ENGINE.md section 15), so this model IS the
 * reference: the exact fixed-point sequence the RTL (tau_cymo_resamp.sv) implements -- one bank lookup
 * per output sample, a 32-tap MAC into a 40-bit signed accumulator, one arithmetic right-shift by 15
 * (Q1.15 coefficients, tools/gen_cymo_resamp_rom.py), then a 16-bit clip -- cross-checked here against an
 * unbounded (int64_t) accumulate to confirm ACC_WIDTH=40 never actually binds (this project's own
 * "prove the width, don't assume it" habit, B-365's own precedent).
 *
 * ALSO drives the phase accumulator (phase += Q=147; wrap at P=160, consuming one new input sample pair
 * on wrap) and a per-channel 32-sample history ring (newest-first, matching tau_flac_lpc.sv's hist[0]-is-
 * newest convention) over a pseudo-random stereo input stream, and writes RTL testbench vectors.
 *
 * Vector file format (build/rtl/cymo_resamp_vectors.txt), fixed-width hex words, one per line:
 *   line 0:            NIN  (number of input stereo samples)
 *   line 1:            NOUT (number of output stereo samples produced)
 *   next NIN*2 lines:  input_l[0], input_r[0], input_l[1], input_r[1], ...  (sign-extended to 32 bits)
 *   next NOUT*3 lines: out_l[k], out_r[k], pop[k]  (pop[k]=1 means: after this output, before computing
 *                      output k+1, push the NEXT unconsumed input sample into history)
 *
 * Usage: cymo_resamp_model <vectors.txt>   Exit 1 on any overflow (ACC_WIDTH=40 proven insufficient). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "cymo_resamp_rom.h"

#define P              CYMO_RESAMP_BANKS   /* 160 */
#define Q_STEP         147                 /* exact 44100:48000 = 160:147 */
#define TAPS           CYMO_RESAMP_TAPS    /* 32 */
#define ACC_WIDTH      40
#define NIN            4000

static uint32_t rng = 20260930u;
static uint32_t rnd32(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static int32_t rnd_s16(void) { return (int32_t)(int16_t)(rnd32() & 0xFFFFu); }

static int16_t clip16(int64_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

int main(int argc, char **argv)
{
    FILE *vf = argc > 1 ? fopen(argv[1], "w") : NULL;

    int32_t in_l[NIN], in_r[NIN];
    for (int i = 0; i < NIN; i++) { in_l[i] = rnd_s16(); in_r[i] = rnd_s16(); }

    /* Upper bound on output count: phase advances by Q<P every step, so at most one input is consumed
     * per output and the ratio P/Q (~1.088) bounds how many outputs one input stream can produce. 2x NIN
     * is a generous, cheap-to-allocate margin. */
    int32_t *out_l = malloc(sizeof(int32_t) * NIN * 2);
    int32_t *out_r = malloc(sizeof(int32_t) * NIN * 2);
    uint8_t *pop   = malloc(sizeof(uint8_t) * NIN * 2);

    int16_t hist[2][TAPS] = {{0}};   /* newest-first ring; reset state is all-zero, matching hardware reset/clear */
    int phase = 0;
    int consumed = 0;
    int nout = 0;
    int overflows = 0;
    const int64_t lo = -((int64_t)1 << (ACC_WIDTH - 1));
    const int64_t hi = ((int64_t)1 << (ACC_WIDTH - 1)) - 1;

    while (consumed < NIN) {
        int64_t acc_l = 0, acc_r = 0;
        const short *bank = &cymo_coef_rom[phase * TAPS];
        for (int t = 0; t < TAPS; t++) {
            acc_l += (int64_t)bank[t] * (int64_t)hist[0][t];
            acc_r += (int64_t)bank[t] * (int64_t)hist[1][t];
        }
        if (acc_l < lo || acc_l > hi || acc_r < lo || acc_r > hi) overflows++;
        out_l[nout] = clip16(acc_l >> 15);
        out_r[nout] = clip16(acc_r >> 15);

        phase += Q_STEP;
        if (phase >= P) {
            phase -= P;
            /* push: shift the ring, newest sample becomes index 0 */
            for (int t = TAPS - 1; t > 0; t--) { hist[0][t] = hist[0][t - 1]; hist[1][t] = hist[1][t - 1]; }
            hist[0][0] = (int16_t)in_l[consumed];
            hist[1][0] = (int16_t)in_r[consumed];
            consumed++;
            pop[nout] = 1;
        } else {
            pop[nout] = 0;
        }
        nout++;
    }

    printf("NIN %d, NOUT %d, overflows %d (ACC_WIDTH=%d)\n", NIN, nout, overflows, ACC_WIDTH);

    if (vf) {
        fprintf(vf, "%08x\n%08x\n", (uint32_t)NIN, (uint32_t)nout);
        for (int i = 0; i < NIN; i++) fprintf(vf, "%08x\n%08x\n", (uint32_t)in_l[i], (uint32_t)in_r[i]);
        for (int k = 0; k < nout; k++) fprintf(vf, "%08x\n%08x\n%08x\n", (uint32_t)out_l[k], (uint32_t)out_r[k], (uint32_t)pop[k]);
        fclose(vf);
    }

    free(out_l); free(out_r); free(pop);
    return overflows ? 1 : 0;
}
