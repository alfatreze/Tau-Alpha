/* End-to-end host check of the B-307 redirect (docs/MP3_FILTERBANK_KERNEL_DESIGN.md section 6 step 4): runs Helix's REAL Subband() (real
 * subband.c, dct32.c, polyphase.c, trigtabs.c) on a fabricated IMDCT output, once as shipped and once with TAU_POLY_FW=1 where the window is
 * "hardware" -- a stub whose tau_poly_hw_slot() is the golden model (sim/mp3_poly_model_core.h, itself proven equal to PolyphaseStereo).
 * Built twice by sim/test_mp3_poly_subband.py; the two binaries must print IDENTICAL PCM.
 *
 * Coverage: normal / loud (clipping) / quiet inputs, guard-bit counts 8..0 (gb < 6 makes FDCT32 take its es fixup path, which rewrites the words
 * that reach the log), a second decoder instance mid-run (must re-clear the unit's history), mono (never redirected), and an injected unit failure
 * at a chosen slot (the rest of the track must finish in software and still match).
 * Usage: harness <fail_slot -1|N>. Prints "PCM <hex>" per granule and, last, "HWSLOTS n". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "coder.h"
#include TABLES_H
#include "mp3_poly_model_core.h"

#if TAU_POLY_FW
int tau_poly_hw_enable = 1;
int tau_poly_wlog[33], tau_poly_wn;
int tau_poly_verify_left;
unsigned tau_poly_stat_slots, tau_poly_stat_mismatch, tau_poly_stat_timeout;
static int hw_slots, fail_slot = -1, slot_no, corrupt_slot = -1;
void tau_poly_hw_clear(void) { memset(ring, 0, sizeof ring); head[0] = head[1] = 0; tau_poly_verify_left = 8; }
int tau_poly_hw_slot(const int *w0, const int *w1, short *pcm)
{
    if (!tau_poly_hw_enable) return 0;
    if (slot_no++ == fail_slot) { tau_poly_hw_enable = 0; return 0; }
    push(0, w0); push(1, w1);
    short l[32], r[32];
    window(0, l); window(1, r);
    for (int k = 0; k < 32; k++) { pcm[2 * k] = l[k]; pcm[2 * k + 1] = r[k]; }
    if (slot_no - 1 == corrupt_slot) pcm[5] = (short)(pcm[5] + 1);          /* a wrong-but-answering unit */
    hw_slots++;
    return 1;
}
#endif

static uint32_t rng = 31337u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

static MP3DecInfo di; static HuffmanInfo hi; static IMDCTInfo mi; static SubbandInfo sbi;
static void fresh(int nch)
{
    memset(&di, 0, sizeof di); memset(&hi, 0, sizeof hi); memset(&mi, 0, sizeof mi); memset(&sbi, 0, sizeof sbi);
    di.nChans = nch; di.HuffmanInfoPS = &hi; di.IMDCTInfoPS = &mi; di.SubbandInfoPS = &sbi;
}
static void dump(const short *pcm, int n)
{
    printf("PCM");
    for (int i = 0; i < n; i++) printf(" %04x", (unsigned)(unsigned short)pcm[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    int fail = argc > 1 ? atoi(argv[1]) : -1;
#if TAU_POLY_FW
    fail_slot = fail;
    corrupt_slot = argc > 2 ? atoi(argv[2]) : -1;
#else
    (void)fail;
#endif
    static short pcm[2 * NBANDS * BLOCK_SIZE];
    /* {amplitude, guard bits}: gb 8 = ordinary; gb 6 and below approach the es fixup; small gb with big data exercises it and clipping */
    static const struct { int amp, gb; } cls[] = { { 1 << 10, 8 }, { 1 << 20, 8 }, { 1 << 22, 8 }, { 1 << 24, 6 }, { 1 << 25, 5 }, { 1 << 26, 4 },
                                                   { 1 << 27, 3 }, { 1 << 28, 2 }, { 1 << 29, 1 }, { 1 << 30, 0 }, { 0, 8 }, { 1 << 25, 6 } };
    const int NCLS = (int)(sizeof cls / sizeof cls[0]);
    for (int track = 0; track < 2; track++) {                        /* track 2 = a NEW decoder instance: the unit's history must be cleared again */
        fresh(2);
        for (int g = 0; g < 4 * NCLS; g++) {
            int c = (g / 4) % NCLS;
            for (int ch = 0; ch < 2; ch++) {
                mi.gb[ch] = cls[c].gb;
                for (int b = 0; b < BLOCK_SIZE; b++) for (int k = 0; k < NBANDS; k++)
                    mi.outBuf[ch][b][k] = cls[c].amp ? (int)(rnd() % (2u * (unsigned)cls[c].amp)) - cls[c].amp : 0;
            }
            Subband(&di, pcm);
            dump(pcm, 2 * NBANDS * BLOCK_SIZE);
        }
    }
    fresh(1);                                                        /* mono: never redirected */
    for (int g = 0; g < 8; g++) {
        mi.gb[0] = 8;
        for (int b = 0; b < BLOCK_SIZE; b++) for (int k = 0; k < NBANDS; k++) mi.outBuf[0][b][k] = (int)(rnd() % (1u << 22)) - (1 << 21);
        Subband(&di, pcm);
        dump(pcm, NBANDS * BLOCK_SIZE);
    }
#if TAU_POLY_FW
    printf("HWSLOTS %d MISMATCH %u\n", hw_slots, tau_poly_stat_mismatch);
#else
    printf("HWSLOTS 0 MISMATCH 0\n");
#endif
    return 0;
}
