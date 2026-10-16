/* Harness for the seek search. Usage: harness <frames.bin> <streaminfo hex> <total> <variable 0|1> <use_table 0|1> <targets csv> -> per target a line "state found_off found_s probes".
 * The "file" is the frame data; the table (when used) is a point list given on stdin: "<n> then n lines: sample offset". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac2_core.h"
int main(int argc, char **argv)
{
    if (argc < 7) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n + 8192); memset(data, 0, (size_t)n + 8192); if (fread(data, 1, (size_t)n, f) != (size_t)n) return 3; fclose(f);
    uint8_t sib[34]; for (int i = 0; i < 34; i++) { unsigned x; sscanf(argv[2] + 2 * i, "%2x", &x); sib[i] = (uint8_t)x; }
    f2_info_t si; if (f2_streaminfo(sib, &si) != F2_OK) return 4;
    if (!strcmp(argv[6], "index")) {                                       /* ground truth: every frame's offset, first sample and size, by decoding the whole stream */
        static int32_t buf[65536], out[2 * 8192]; f2_dec_t *d = calloc(1, sizeof *d); f2_dec_init(d, &si, buf, 65536, F2_FLAG_CRC16); uint32_t off = 0;
        while (off + 8 < (uint32_t)n) {
            f2_fh_t hh; uint64_t smp = 0; if (f2_frame_header(data + off, (uint32_t)n - off, &si, &hh) != F2_OK) break; f2_frame_sample(&si, &hh, &smp);
            printf("%u %llu %u\n", off, (unsigned long long)smp, hh.bs);
            uint32_t used = 0, np = 0; do { int st = f2_dec_step(d, data + off, (uint32_t)n - off, out, 8192, 1u << 20, &used, &np); off += used; if (st == F2_ERR || st == F2_NEED) { if (!d->frame_done) { off = (uint32_t)n; } break; } } while (!d->frame_done);
        }
        return 0;
    }
    uint64_t total = strtoull(argv[3], 0, 10); int var = atoi(argv[4]), use_table = atoi(argv[5]);
    uint32_t np = 0; static uint64_t ts[4096], to[4096]; if (use_table) { if (scanf("%u", &np) == 1) for (uint32_t i = 0; i < np; i++) { unsigned long long a, b; scanf("%llu %llu", &a, &b); ts[i] = a; to[i] = b; } }
    const char *s = argv[6];
    while (*s) {
        uint64_t target = strtoull(s, (char **)&s, 10); if (*s == ',') s++;
        uint64_t lo_off = 0, lo_s = 0; for (uint32_t i = 0; i < np; i++) if (ts[i] <= target) { lo_s = ts[i]; lo_off = to[i]; }
        f2_seek_t k; int st = f2_seek_begin(&k, &si, target, total, (uint64_t)n, lo_off, lo_s, var);
        while (st == F2_SEEK_CONT) {
            uint32_t avail = (uint64_t)n - k.probe < F2_SEEK_WIN ? (uint32_t)((uint64_t)n - k.probe) : F2_SEEK_WIN;
            st = f2_seek_feed(&k, &si, data + k.probe, avail);
        }
        printf("%d %llu %llu %u\n", st, (unsigned long long)k.found_off, (unsigned long long)k.found_s, k.probes);
    }
    return 0;
}
