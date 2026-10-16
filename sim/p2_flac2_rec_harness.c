/* Harness for the recovery layer f2_rec_step (fw2/cores/flac2_core.h). Same arguments as p2_flac2_dec_harness.c. The driver plays the glue: pairs are STAGED per frame and committed on
 * F2_EV_FRAME_OK, dropped on F2_EV_FRAME_BAD, silence is committed at once; at the end of the data the output is padded with silence up to STREAMINFO's total (when known).
 * Stats line: status frames_ok bad_events resyncs silent_pairs frame_errors giveup committed_pairs */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac2_core.h"
static uint32_t csv(const char *s, uint32_t *o, uint32_t max) { uint32_t n = 0; while (*s && n < max) { o[n++] = (uint32_t)strtoul(s, (char **)&s, 10); if (*s == ',') s++; } return n; }
static uint8_t *slurp(const char *path, uint32_t *n) { FILE *f = fopen(path, "rb"); if (!f) exit(3); fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc((size_t)l + 16); if (fread(b, 1, (size_t)l, f) != (size_t)l) exit(3); fclose(f); *n = (uint32_t)l; return b; }
int main(int argc, char **argv)
{
    if (argc < 9) return 2;
    uint32_t n, ch[64], sl[64], oc[64], nc = csv(argv[3], ch, 64), ns = csv(argv[4], sl, 64), no = csv(argv[5], oc, 64), flags = (uint32_t)atoi(argv[6]);
    uint8_t *data = slurp(argv[1], &n), si_b[34] = {0};
    for (int i = 0; i < 34; i++) { unsigned x; sscanf(argv[2] + 2 * i, "%2x", &x); si_b[i] = (uint8_t)x; }
    f2_info_t si; if (f2_streaminfo(si_b, &si) != F2_OK) return 4;
    static int32_t buf[65536], pend[2 * 70000], tmp[2 * 70000]; f2_rec_t *r = calloc(1, sizeof *r);
    f2_rec_init(r, &si, buf, 65536, flags);
    FILE *po = fopen(argv[7], "wb"); uint32_t off = 0, ci = 0, si_i = 0, oi = 0, grow = 0, npend = 0, okf = 0, badf = 0, giveup = 0, spins = 0; uint64_t committed = 0; int fin = 0, status = 0;
    while (!fin && ++spins < 20000000u) {
        uint32_t len = ch[ci % nc] + grow; if (off + len > n) len = n - off; uint32_t cap = oc[oi % no], used = 0, np = 0; int ev = 0;
        int st = f2_rec_step(r, data + off, len, tmp, cap, sl[si_i % ns], &used, &np, &ev);
        ci++; si_i++; oi++; off += used;
        if (committed > 50000000ull) { status = 7; break; }                                   /* runaway silence: a defect, not a stream */
        if (ev & F2_EV_SILENCE) { fwrite(tmp, 8, np, po); committed += np; }
        else if (ev & F2_EV_FRAME_BAD) { npend = 0; badf++; }
        else {
            if (npend + np > 70000u) { status = 9; break; }
            memcpy(pend + 2 * npend, tmp, 8u * np); npend += np;
            if (ev & F2_EV_FRAME_OK) { fwrite(pend, 8, npend, po); committed += npend; npend = 0; okf++; }
        }
        if (ev & F2_EV_GIVEUP) { giveup = 1; status = 2; fin = 1; }
        else if (st == F2_NEED) { if (off >= n) fin = 1; else grow = (used || np) ? 0u : len; }
        else grow = 0;
    }
    if (spins >= 20000000u) status = 8;
    /* the end of the data: a frame in progress is lost; silence up to the known total keeps put == timeline */
    if (npend) npend = 0;
    if (si.total && committed < si.total) { uint64_t miss = si.total - committed; static int32_t z[2 * 4096]; memset(z, 0, sizeof z); while (miss) { uint32_t k = miss > 4096u ? 4096u : (uint32_t)miss; fwrite(z, 8, k, po); miss -= k; committed += k; } }
    fclose(po);
    FILE *so = fopen(argv[8], "w"); fprintf(so, "%d %u %u %u %llu %u %u %llu\n", status, okf, badf, r->resyncs, (unsigned long long)r->silent_pairs, r->frame_errors, giveup, (unsigned long long)committed); fclose(so);
    return 0;
}
