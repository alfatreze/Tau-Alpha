/* Harness for the open/metadata walker. Usage: harness <file> <chunks csv> <seek_cap> <vc_cap> <skip 0|1> -> one line:
 * status err state audio_off | rate ch bps total min max | have_seek seek_len seek_total seek_trunc seek_off fnv | have_vc vc_len vc_total vc_trunc vc_off fnv | consumed */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac2_core.h"
static uint32_t fnv(const uint8_t *b, uint32_t n) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; } return h; }
int main(int argc, char **argv)
{
    if (argc < 6) return 2;
    uint32_t ch[64], nc = 0; const char *s = argv[2]; while (*s && nc < 64) { ch[nc++] = (uint32_t)strtoul(s, (char **)&s, 10); if (*s == ',') s++; }
    uint32_t scap = (uint32_t)atoi(argv[3]), vcap = (uint32_t)atoi(argv[4]); int skip = atoi(argv[5]);
    FILE *f = fopen(argv[1], "rb"); if (!f) return 3; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)n + 16); if (fread(data, 1, (size_t)n, f) != (size_t)n) return 3; fclose(f);
    uint8_t *sb = malloc(scap + 1), *vb = malloc(vcap + 1); f2_open_t *o = calloc(1, sizeof *o); f2_open_init(o, sb, scap, vb, vcap);
    uint32_t off = 0, ci = 0, grow = 0, spins = 0; int st = F2_NEED;
    while (off < (uint32_t)n && o->state != F2_OP_DONE && ++spins < 50000000u) {
        if (skip && f2_open_skippable(o) > 64u) { uint32_t k = f2_open_skippable(o); if (off + k > (uint32_t)n) k = (uint32_t)n - off; off += k; if (f2_open_skipped(o, k) != F2_OK) { st = F2_ERR; break; } continue; }
        uint32_t len = ch[ci++ % nc] + grow; if (off + len > (uint32_t)n) len = (uint32_t)n - off; uint32_t used = 0;
        st = f2_open_step(o, data + off, len, &used); off += used; grow = 0;
        if (st == F2_ERR) break;
    }
    if (o->state == F2_OP_DONE) st = F2_OK;
    printf("%d %u %u %llu | %u %u %u %llu %u %u | %u %u %u %u %llu %u | %u %u %u %u %llu %u | %u\n", st, o->err, o->state, (unsigned long long)o->audio_off, o->si.rate, o->si.channels, o->si.bps, (unsigned long long)o->si.total, o->si.min_bs, o->si.max_bs,
           o->have_seek, o->seek_len, o->seek_total, o->seek_trunc, (unsigned long long)o->seek_off, fnv(sb, o->seek_len), o->have_vc, o->vc_len, o->vc_total, o->vc_trunc, (unsigned long long)o->vc_off, fnv(vb, o->vc_len), off);
    return 0;
}
