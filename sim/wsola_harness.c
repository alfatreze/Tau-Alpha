/* Host harness for fw/wsola_core.h (B-549): reads raw little-endian int16 (interleaved if stereo), runs the core over the whole input, writes the output.
 * usage: wsola_harness <in.raw> <out.raw> <rate> <channels> <speed_q8> ; prints "macs grains" to stderr. */
#include <stdio.h>
#include <stdlib.h>
#define WS_COUNT
#include "../fw/wsola_core.h"
int main(int argc, char **argv)
{
    if (argc < 6) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3;
    fseek(f, 0, SEEK_END); long bytes = ftell(f); fseek(f, 0, SEEK_SET);
    const unsigned fs = (unsigned)atoi(argv[3]), ch = (unsigned)atoi(argv[4]), spd = (unsigned)atoi(argv[5]);
    const long n = bytes / 2 / ch;
    int16_t *raw = malloc((size_t)bytes), *l = malloc(n * 2), *r = malloc(n * 2);
    if (fread(raw, 2, (size_t)(n * ch), f) != (size_t)(n * ch)) return 4;
    fclose(f);
    for (long i = 0; i < n; i++) { l[i] = raw[i * ch]; r[i] = ch == 2 ? raw[i * ch + 1] : 0; }
    FILE *o = fopen(argv[2], "wb"); if (!o) return 5;
    ws_t s; ws_init(&s, fs, (uint8_t)ch, spd);
    int16_t ol[WS_MAX_HS], orr[WS_MAX_HS], buf[2 * WS_MAX_HS];
    for (;;) {
        uint32_t lo, hi; ws_need(&s, &lo, &hi);
        if (hi > (uint32_t)n) break;
        const uint32_t h = ws_step(&s, l, ch == 2 ? r : 0, 0, ol, orr);
        for (uint32_t i = 0; i < h; i++) { buf[i * ch] = ol[i]; if (ch == 2) buf[i * ch + 1] = orr[i]; }
        fwrite(buf, 2, (size_t)h * ch, o);
    }
    fclose(o);
    fprintf(stderr, "%u %u\n", ws_macs, s.k);
    return 0;
}
