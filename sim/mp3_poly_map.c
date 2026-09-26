/* Derives, from Helix's REAL FDCT32/PolyphaseStereo, the tap map the hardware window unit needs (B-292): for every vbuf position the window reads
 * (17 rows x taps: tap x = offset x, tap 8+x = offset 23-x, either channel) which FDCT32 output word it holds -- its AGE in slots (0 = the slot just pushed) and its index P
 * in the push order (the order FDCT32's final stage produces its 32 unique words: P0 = sample 0, P1..16 = samples 16..31, P17..31 = samples 15..1).
 * The map must be identical for every slot, every 8-phase vindex and both block parities, and identical for the left and right channel; anything else
 * is reported and exits 1. Prints the table (17 rows x 16 taps) as "row tap age P" lines between MAP BEGIN/END for the ROM generator. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coder.h"
extern void FDCT32(int *buf, int *dest, int offset, int oddBlock, int gb);
extern void PolyphaseStereo(short *pcm, int *vbuf, const int *coefBase);
int wlog[64], wn;                                     /* filled by the instrumented FDCT32 (the harness build patches dct32.c) */
#define NSLOT 300
static uint32_t rng = 777u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static int ring[NSLOT][2][32];                        /* per slot, per channel: the words in push order */
int main(void)
{
    static int vbuf[MAX_NCHAN * VBUF_LENGTH];
    int vindex = 0, bad = 0;
    int map_age[17][16][2], map_p[17][16][2], set[17][16][2];
    memset(set, 0, sizeof set);
    for (int s = 0; s < NSLOT; s++) {
        int b = s;
        for (int ch = 0; ch < 2; ch++) {
            int in[32];
            for (int i = 0; i < 32; i++) in[i] = ((int)(rnd() & 0x3FFFFF) - 0x200000) | 1;
            wn = 0;
            FDCT32(in, vbuf + ch * 32, vindex, b & 1, 8);
            if (wn != 33 || wlog[17] != wlog[1]) { printf("unexpected write log: %d entries\n", wn); return 1; }
            for (int k = 0; k < 33; k++) { if (k == 17) continue; ring[s][ch][k < 17 ? k : k - 1] = wlog[k]; }
        }
        int *base = vbuf + vindex + VBUF_LENGTH * (b & 1);
        if (s >= 20) {
            for (int r = 0; r < 17; r++)
                for (int t = 0; t < 16; t++) {
                    if (r == 16 && t >= 8) continue;                       /* the sample-16 row only uses macros 0..7 (MC1) */
                    int o = t < 8 ? t : 23 - (t - 8);                          /* macro x reads vb1+x (tap x) and vb1+23-x (tap 8+x) */
                    for (int ch = 0; ch < 2; ch++) {
                        int v = base[64 * r + o + 32 * ch], fa = -1, fp = -1, n = 0;
                        for (int a = 0; a < 17; a++) for (int p = 0; p < 32; p++) if (ring[s - a][ch][p] == v) { fa = a; fp = p; n++; }
                        if (n == 0) { if (bad++ < 5) printf("slot %d row %d tap %d ch %d: value matches NO word of the last 17 slots\n", s, r, t, ch); continue; }
                        if (n > 1) continue;                                   /* two words happen to be equal (random data): no information */
                        if (!set[r][t][ch]) { set[r][t][ch] = 1; map_age[r][t][ch] = fa; map_p[r][t][ch] = fp; }
                        else if (map_age[r][t][ch] != fa || map_p[r][t][ch] != fp) { if (bad++ < 5) printf("slot %d row %d tap %d ch %d: map changed\n", s, r, t, ch); }
                    }
                }
        }
        vindex = (vindex - (b & 1)) & 7;
    }
    for (int r = 0; r < 17; r++) for (int t = 0; t < 16; t++) {
        if (r == 16 && t >= 8) continue;
        if (map_age[r][t][0] != map_age[r][t][1] || map_p[r][t][0] != map_p[r][t][1]) { if (bad++ < 5) printf("row %d tap %d: L and R maps differ\n", r, t); }
    }
    printf("MAP BEGIN\n");
    for (int r = 0; r < 17; r++) for (int t = 0; t < 16; t++) { if (r == 16 && t >= 8) continue; printf("%d %d %d %d\n", r, t, map_age[r][t][0], map_p[r][t][0]); }
    printf("MAP END\nbad: %d\n", bad);
    return bad ? 1 : 0;
}
