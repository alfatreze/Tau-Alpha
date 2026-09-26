/* Golden model of the hardware MP3 window unit (B-292), diffed against Helix's REAL FDCT32 + PolyphaseStereo.
 *
 * The model is the algorithm the RTL implements, written the way the RTL sequences it: a per-channel ring of 16 slots x 32 words (the words
 * FDCT32 emits, in push order), the ROM tap map (age, P) and the 264 window coefficients, the same 64-bit multiply-accumulate, the same rounding
 * constant, arithmetic shift and clip. For every slot it must reproduce PolyphaseStereo's 64 PCM shorts exactly. Also writes the vectors the RTL
 * testbench replays (sim/tb_tau_mp3_poly.v): per slot 64 input words (L then R, push order) and 32 expected words (L | R << 16).
 * Usage: model <tables.h> <vectors.txt>   (tables.h from tools/gen_mp3_poly_rom.py --h). Exit 1 on any PCM difference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "coder.h"
extern void FDCT32(int *buf, int *dest, int offset, int oddBlock, int gb);
extern void PolyphaseStereo(short *pcm, int *vbuf, const int *coefBase);
int wlog[64], wn;
#include TABLES_H

#include "mp3_poly_model_core.h"

static uint32_t rng = 99u;
static uint32_t rnd32(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *vf = fopen(argv[1], "w");
    static int vbuf[MAX_NCHAN * VBUF_LENGTH];
    int vindex = 0, diffs = 0, slots = 0, clipped = 0;
    /* amplitude classes: {mask, gb, bias mode}: quiet, normal, loud, full scale (gb 6), impulse, silence, alternating extremes */
    static const struct { int amp, gb, mode; int n; } cls[] = {
        { 1 << 10, 8, 0, 20 }, { 1 << 18, 8, 0, 30 }, { 1 << 22, 8, 0, 30 }, { 1 << 25, 6, 0, 30 }, { 1 << 25, 6, 1, 20 }, { 0, 8, 2, 20 }, { 1 << 25, 6, 3, 20 } };
    for (unsigned c = 0; c < sizeof cls / sizeof cls[0]; c++)
        for (int n = 0; n < cls[c].n; n++) {
            int b = slots, words[2][32];
            for (int ch = 0; ch < 2; ch++) {
                int in[32];
                for (int i = 0; i < 32; i++) {
                    int v = 0;
                    if (cls[c].mode == 0) v = (int)(rnd32() % (2u * cls[c].amp)) - cls[c].amp;
                    else if (cls[c].mode == 1) v = (i == (n % 32)) ? cls[c].amp - 1 : 0;                 /* a moving impulse */
                    else if (cls[c].mode == 3) v = ((i + n) & 1) ? cls[c].amp - 1 : -cls[c].amp;        /* alternating extremes */
                    in[i] = v;
                }
                int tmp[32]; memcpy(tmp, in, sizeof tmp);
                wn = 0;
                FDCT32(tmp, vbuf + ch * 32, vindex, b & 1, cls[c].gb);
                for (int k = 0; k < 33; k++) { if (k == 17) continue; words[ch][k < 17 ? k : k - 1] = wlog[k]; }
                push(ch, words[ch]);
            }
            short ref[64], mine[2][32];
            PolyphaseStereo(ref, vbuf + vindex + VBUF_LENGTH * (b & 1), polyCoef);
            for (int ch = 0; ch < 2; ch++) window(ch, mine[ch]);
            for (int k = 0; k < 32; k++) for (int ch = 0; ch < 2; ch++) {
                if (ref[2 * k + ch] != mine[ch][k]) { if (diffs++ < 8) printf("slot %d class %u ch %d sample %d: helix %d model %d\n", slots, c, ch, k, ref[2 * k + ch], mine[ch][k]); }
                if (ref[2 * k + ch] == 32767 || ref[2 * k + ch] == -32768) clipped++;
            }
            for (int ch = 0; ch < 2; ch++) for (int k = 0; k < 32; k++) fprintf(vf, "%08x\n", (unsigned)words[ch][k]);
            for (int k = 0; k < 32; k++) fprintf(vf, "%08x\n", ((unsigned)(unsigned short)ref[2 * k]) | ((unsigned)(unsigned short)ref[2 * k + 1] << 16));
            vindex = (vindex - (b & 1)) & 7;
            slots++;
        }
    fclose(vf);
    printf("slots %d, samples clipped %d, differences %d\n", slots, clipped, diffs);
    return diffs ? 1 : 0;
}
