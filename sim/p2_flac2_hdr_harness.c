/* Harness for fw2/cores/flac2_core.h piece 3 (CRCs, STREAMINFO, frame header). One command per stdin line:
 *   C8 <hex>                         -> crc8
 *   C16 <init> <hex> <split>         -> crc16, the data fed in pieces of <split> bytes (state carried)
 *   SI <hex34>                       -> status rate channels bps total min_bs max_bs
 *   H <hex34 streaminfo> <hex> <avail>  -> status err hdr_len bs rate number variable assign chans bps   (only the first <avail> bytes are visible) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac2_core.h"
static uint32_t unhex(const char *h, uint8_t *o) { uint32_t n = 0; if (!h || !strcmp(h, "-")) return 0; for (; h[2 * n] && h[2 * n + 1]; n++) { unsigned x; sscanf(h + 2 * n, "%2x", &x); o[n] = (uint8_t)x; } return n; }
int main(void)
{
    static char line[1 << 16]; static uint8_t a[1 << 15], b[64];
    while (fgets(line, sizeof line, stdin)) {
        char *cmd = strtok(line, " \n");
        if (!strcmp(cmd, "C8")) { uint32_t n = unhex(strtok(0, " \n"), a); printf("%u\n", f2_crc8(a, n)); }
        else if (!strcmp(cmd, "C16")) { unsigned init = (unsigned)atoi(strtok(0, " \n")); uint32_t n = unhex(strtok(0, " \n"), a), sp = (uint32_t)atoi(strtok(0, " \n")); uint16_t c = (uint16_t)init;
            for (uint32_t i = 0; i < n; i += sp) c = f2_crc16(c, a + i, i + sp < n ? sp : n - i); printf("%u\n", c); }
        else if (!strcmp(cmd, "SI")) { unhex(strtok(0, " \n"), a); f2_info_t si; int r = f2_streaminfo(a, &si); printf("%d %u %u %u %llu %u %u\n", r, si.rate, si.channels, si.bps, (unsigned long long)si.total, si.min_bs, si.max_bs); }
        else if (!strcmp(cmd, "H")) { unhex(strtok(0, " \n"), b); uint32_t n = unhex(strtok(0, " \n"), a), av = (uint32_t)atoi(strtok(0, " \n")); f2_info_t si; f2_streaminfo(b, &si);
            f2_fh_t h; memset(&h, 0, sizeof h); int r = f2_frame_header(a, av < n ? av : n, &si, &h);
            printf("%d %d %u %u %u %llu %u %u %u %u\n", r, h.err, h.hdr_len, h.bs, h.rate, (unsigned long long)h.number, h.variable, h.assign, h.chans, h.bps); }
    }
    return 0;
}
