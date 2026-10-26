/* Host harness for the v2 core (B-556): feeds the input in chunks of a given size, steps whenever ws2_ready() allows, reads full-rate samples from the input array
 * through the reader callback, writes the output. usage: wsola2_harness <in.raw> <out.raw> <rate> <channels> <speed_q8> <chunk> [slice] ; stderr: "macs grains overrun". */
#include <stdio.h>
#include <stdlib.h>
#define WS_COUNT
#include "../fw/wsola_core.h"
static int16_t *gl, *gr;
static void rd(void *ctx, uint32_t ch, uint32_t lo, uint32_t n, int16_t *dst)
{ (void)ctx; const int16_t *src = ch ? gr : gl; for (uint32_t i = 0; i < n; i++) dst[i] = src[lo + i]; }
int main(int argc, char **argv)
{
    if (argc < 7) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3;
    fseek(f, 0, SEEK_END); long bytes = ftell(f); fseek(f, 0, SEEK_SET);
    const unsigned slice = argc > 7 ? (unsigned)atoi(argv[7]) : 0u;
    const unsigned fs = (unsigned)atoi(argv[3]), ch = (unsigned)atoi(argv[4]), spd = (unsigned)atoi(argv[5]), chunk = (unsigned)atoi(argv[6]);
    const long n = bytes / 2 / ch;
    int16_t *raw = malloc((size_t)bytes); gl = malloc(n * 2); gr = malloc(n * 2);
    if (fread(raw, 2, (size_t)(n * ch), f) != (size_t)(n * ch)) return 4;
    fclose(f);
    for (long i = 0; i < n; i++) { gl[i] = raw[i * ch]; gr[i] = ch == 2 ? raw[i * ch + 1] : 0; }
    FILE *o = fopen(argv[2], "wb"); if (!o) return 5;
    static ws2_t s; ws2_init(&s, fs, (uint8_t)ch, spd);
    int16_t ol[WS_MAX_HS], orr[WS_MAX_HS], ob[2 * WS_MAX_HS];
    long pos = 0; unsigned overrun = 0;
    while (pos < n) {
        long m = n - pos < (long)chunk ? n - pos : (long)chunk;
        ws2_feed(&s, gl + pos, ch == 2 ? gr + pos : 0, (uint32_t)m); pos += m;
        for (;;) {
            if (!ws2_ready(&s)) { uint32_t lo, hi; ws_need(&s.core, &lo, &hi); if (s.fed >= hi) overrun++; break; }
            uint32_t h;
            if (slice) {       /* RAM diet: the hop in slices (begin / emit / end), the same samples as the whole-hop call */
                uint32_t cg; h = ws2_step_begin(&s, rd, 0, &cg);
                for (uint32_t a = 0; a < h; a += slice) {
                    const uint32_t m2 = h - a < slice ? h - a : slice;
                    ws2_step_emit(&s, rd, 0, cg, a, m2, ol + a, orr + a);
                }
                ws2_step_end(&s, rd, 0, cg);
            } else h = ws2_step(&s, rd, 0, ol, orr);
            for (uint32_t i = 0; i < h; i++) { ob[i * ch] = ol[i]; if (ch == 2) ob[i * ch + 1] = orr[i]; }
            fwrite(ob, 2, (size_t)h * ch, o);
        }
        if (overrun) break;
    }
    fclose(o);
    fprintf(stderr, "%u %u %u\n", ws_macs, s.core.k, overrun);
    return 0;
}
