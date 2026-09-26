/* Host probe for docs/MP3_FILTERBANK_KERNEL_DESIGN.md: how much V history does Helix's polyphase window REALLY read, and is every word it
 * reads one of the unique FDCT32 outputs of the last few slots (so a hardware unit can store only those)?
 *
 * Method (no changes to Helix): run the real FDCT32 + PolyphaseStereo exactly as subband.c does, on random data. For every slot:
 *   1. the read set R: every vbuf position whose perturbation changes the PCM output (the window is linear in V);
 *   2. the logical words: the distinct values each FDCT32 call writes (collected from a scratch buffer);
 *   3. every position in R must hold a value written by FDCT32 of one of the last 17 slots of the SAME channel; the number of distinct such
 *      (slot, value) words is the storage a hardware unit needs.
 * Prints: per-channel words needed by age, the maximum per channel, and any violation. Exit 1 on a violation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "coder.h"

#define NSLOT 200
#define MAXW  64

extern void FDCT32(int *buf, int *dest, int offset, int oddBlock, int gb);
extern void PolyphaseStereo(short *pcm, int *vbuf, const int *coefBase);

static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

int main(void)
{
    static int vbuf[MAX_NCHAN * VBUF_LENGTH];
    int vindex = 0;
    int nvals[NSLOT][2]; int vals[NSLOT][2][MAXW];
    int maxw[2] = {0, 0}, viol = 0, maxage = 0, maxdist_total[2] = {0, 0};
    int age_hist[2][32]; memset(age_hist, 0, sizeof age_hist);
    int max_per_age[2][32]; memset(max_per_age, 0, sizeof max_per_age);
    int words_seen_max = 0;
    static int ever[2][NSLOT][MAXW];                                 /* lifetime: which words of each slot are read at ANY age */
    memset(vbuf, 0, sizeof vbuf);

    for (int s = 0; s < NSLOT; s++) {
        const int b = s;                                            /* block index parity as in Subband() */
        int in[2][32];
        for (int ch = 0; ch < 2; ch++) for (int i = 0; i < 32; i++) in[ch][i] = ((int)(rnd() & 0x3FFFFF) - 0x200000) | 1;
        for (int ch = 0; ch < 2; ch++) {
            int tmp[32]; memcpy(tmp, in[ch], sizeof tmp);
            static int scratch[MAX_NCHAN * VBUF_LENGTH];
            memset(scratch, 0, sizeof scratch);
            FDCT32(tmp, scratch + ch * 32, vindex, b & 1, 8);          /* the logical words of this call */
            int n = 0;
            for (int p = 0; p < MAX_NCHAN * VBUF_LENGTH; p++)
                if (scratch[p]) { int dup = 0; for (int k = 0; k < n; k++) if (vals[s][ch][k] == scratch[p]) dup = 1; if (!dup && n < MAXW) vals[s][ch][n++] = scratch[p]; }
            nvals[s][ch] = n;
            if (n > maxw[ch]) maxw[ch] = n;
            memcpy(tmp, in[ch], sizeof tmp);
            FDCT32(tmp, vbuf + ch * 32, vindex, b & 1, 8);              /* the real state */
        }
        int *base = vbuf + vindex + VBUF_LENGTH * (b & 1);
        short pcm0[64], pcm1[64];
        PolyphaseStereo(pcm0, base, polyCoef);
        int used[MAX_NCHAN * VBUF_LENGTH]; memset(used, 0, sizeof used);
        for (int p = 0; p < MAX_NCHAN * VBUF_LENGTH; p++) {
            int keep = vbuf[p], hit = 0;
            for (int sg = -1; sg <= 1 && !hit; sg += 2) {
                vbuf[p] = keep + sg * (1 << 26);
                PolyphaseStereo(pcm1, base, polyCoef);
                if (memcmp(pcm0, pcm1, sizeof(short) * 64)) hit = 1;
            }
            vbuf[p] = keep;
            used[p] = hit;
        }
        if (s >= 40) {                                                  /* steady state */
            int distinct[2] = {0, 0};
            static int seen[2][NSLOT][MAXW];
            memset(seen, 0, sizeof seen);
            int per_age[2][32]; memset(per_age, 0, sizeof per_age);
            for (int p = 0; p < MAX_NCHAN * VBUF_LENGTH; p++) {
                if (!used[p]) continue;
                int found = 0;
                for (int ch = 0; ch < 2 && !found; ch++)
                    for (int a = 0; a < 20 && !found; a++) {
                        int sl = s - a; if (sl < 0) break;
                        for (int k = 0; k < nvals[sl][ch]; k++)
                            if (vals[sl][ch][k] == vbuf[p]) {
                                found = 1;
                                if (!seen[ch][sl][k]) { seen[ch][sl][k] = 1; distinct[ch]++; per_age[ch][a]++; }
                                ever[ch][sl][k] = 1;
                                if (a > maxage) maxage = a;
                                break;
                            }
                    }
                if (!found) { viol++; if (viol < 6) printf("VIOLATION slot %d: read position %d holds a value no recent FDCT32 wrote\n", s, p); }
            }
            for (int ch = 0; ch < 2; ch++) {
                if (distinct[ch] > maxdist_total[ch]) maxdist_total[ch] = distinct[ch];
                for (int a = 0; a < 32; a++) if (per_age[ch][a] > max_per_age[ch][a]) max_per_age[ch][a] = per_age[ch][a];
            }
        }
        vindex = (vindex - (b & 1)) & 7;
    }
    printf("unique words per FDCT32 call: max %d (L), %d (R)\n", maxw[0], maxw[1]);
    printf("distinct history words the window reads (steady state): max %d (L), %d (R); oldest age used: %d slots\n", maxdist_total[0], maxdist_total[1], maxage);
    printf("max words used per age (L):"); for (int a = 0; a <= maxage; a++) printf(" %d", max_per_age[0][a]); printf("\n");
    { int mx[2] = {0, 0}, mn[2] = {99, 99};
      for (int ch = 0; ch < 2; ch++) for (int sl = 60; sl < NSLOT - 20; sl++) {
          int c = 0; for (int k = 0; k < nvals[sl][ch]; k++) c += ever[ch][sl][k];
          if (c > mx[ch]) mx[ch] = c; if (c < mn[ch]) mn[ch] = c; }
      printf("words of ONE slot read over its whole life (any age): min %d / max %d (L), min %d / max %d (R)\n", mn[0], mx[0], mn[1], mx[1]); }
    printf("violations: %d\n", viol);
    (void)words_seen_max; (void)age_hist;
    return viol ? 1 : 0;
}
