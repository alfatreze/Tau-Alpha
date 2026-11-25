/* Host proof of the mono-through-the-window-unit change (B-1172): the REAL Helix decoder (mp3dec.c + real/*.c from tools/helix_tree.py: pristine + tau.patch) decodes a real
 * MP3 file and writes every output sample to a file. Built twice by sim/test_mp3_poly_mono.py:
 *   - TAU_POLY_FW=0                      : today's software window (PolyphaseMono / PolyphaseStereo) -- the reference;
 *   - TAU_POLY_FW=1 TAU_POLY_REQUIRED=1  : Player 2's build. The "unit" is a stub whose window is the golden model (sim/mp3_poly_model_core.h, proven equal to Helix's
 *                                          PolyphaseStereo by sim/test_mp3_poly_model.py), with the real unit's protocol (no push while busy, no read without a slot in flight).
 * Usage: harness <file.mp3> <pcm-out> [fail_slot|-1] [unit_absent 0|1] [finish_fail_slot|-1] [passes] [max_frames_per_pass]. Each pass is a NEW decoder instance over the same file (the unit's history must be cleared again). Prints "FRAMES n CH c SLOTS s SILENT k" on stdout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mp3dec.h"
#include "coder.h"
#include TABLES_H
#include "mp3_poly_model_core.h"

#if TAU_POLY_FW
int tau_poly_hw_enable = 1;
int tau_poly_wlog[33], tau_poly_wn;
int tau_poly_verify_left;
unsigned tau_poly_stat_slots, tau_poly_stat_mismatch, tau_poly_stat_timeout, tau_poly_stat_silent;
static int fail_slot = -1, finish_fail_slot = -1, slot_no, pending, staged_slot = -1, clears;
static short staged[64];
void tau_poly_hw_clear(void) { memset(ring, 0, sizeof ring); head[0] = head[1] = 0; tau_poly_verify_left = 8; clears++; }
int tau_poly_hw_start(const int *w0, const int *w1)
{
    if (!tau_poly_hw_enable) return 0;
    if (pending) { fprintf(stderr, "PROTOCOL: start while a slot is in flight\n"); exit(2); }
    if (slot_no++ == fail_slot) { tau_poly_hw_enable = 0; return 0; }
    push(0, w0); push(1, w1);
    short l[32], r[32]; window(0, l); window(1, r);
    for (int k = 0; k < 32; k++) { staged[2 * k] = l[k]; staged[2 * k + 1] = r[k]; }
    staged_slot = slot_no - 1; pending = 1;
    return 1;
}
int tau_poly_hw_finish(short *pcm)
{
    if (!pending) { fprintf(stderr, "PROTOCOL: finish with nothing in flight\n"); exit(2); }
    pending = 0;
    if (staged_slot == finish_fail_slot) { tau_poly_hw_enable = 0; return 0; }
    memcpy(pcm, staged, sizeof staged);
    tau_poly_stat_slots++;
    return 1;
}
int tau_poly_hw_slot(const int *w0, const int *w1, short *pcm) { return tau_poly_hw_start(w0, w1) && tau_poly_hw_finish(pcm); }
#endif

#define FILEMAX (8u << 20)
static unsigned char buf[FILEMAX];
static short pcm[2304];

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    unsigned size = (unsigned)fread(buf, 1, FILEMAX, f); fclose(f);
#if TAU_POLY_FW
    fail_slot = argc > 3 ? atoi(argv[3]) : -1;
    if (argc > 4 && atoi(argv[4])) tau_poly_hw_enable = 0;
    finish_fail_slot = argc > 5 ? atoi(argv[5]) : -1;
#endif
    FILE *o = fopen(argv[2], "wb"); if (!o) return 2;
    int passes = argc > 6 ? atoi(argv[6]) : 1;
    unsigned maxf = argc > 7 ? (unsigned)atoi(argv[7]) : ~0u;
    unsigned frames = 0; int ch = 0;
    for (int pass = 0; pass < passes; pass++) {
    HMP3Decoder d = MP3InitDecoder();
    unsigned char *p = buf; int left = (int)size;
    if (p[0] == 'I' && p[1] == 'D' && p[2] == '3') { int skip = 10 + ((p[6] << 21) | (p[7] << 14) | (p[8] << 7) | p[9]); p += skip; left -= skip; }
    unsigned pf = 0;
    for (; pf < maxf;) {
        int off = MP3FindSyncWord(p, left); if (off < 0) break; p += off; left -= off;
        int err = MP3Decode(d, &p, &left, pcm, 0);
        if (err) { if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) { if (left < 4) break; continue; } break; }
        MP3FrameInfo fi; MP3GetLastFrameInfo(d, &fi);
        ch = fi.nChans; fwrite(pcm, sizeof(short), (size_t)fi.outputSamps, o); frames++; pf++;
    }
    MP3FreeDecoder(d);
    }
    fclose(o);
#if TAU_POLY_FW
    printf("FRAMES %u CH %d SLOTS %u SILENT %u CLEARS %d\n", frames, ch, tau_poly_stat_slots, tau_poly_stat_silent, clears);
#else
    printf("FRAMES %u CH %d SLOTS 0 SILENT 0 CLEARS 0\n", frames, ch);
#endif
    return 0;
}
