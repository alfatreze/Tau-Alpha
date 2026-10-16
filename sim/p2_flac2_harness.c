/* Harness for fw2/cores/flac2_core.h (piece 1: bit reader + Rice residuals). stdin lines: "<order> <bs> <hex blob> <nchunks> c1 c2 ... <nslices> s1 s2 ..."
 * (chunk sizes of the input windows presented in turn, cycling; slice sizes in residuals per call, cycling). Output: "<status> <count> <consumed_bits> <fnv of the values>" then the values. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flac2_core.h"
static uint32_t fnv(const int32_t *v, uint32_t n) { uint32_t h = 2166136261u; for (uint32_t i = 0; i < n; i++) { h ^= (uint32_t)v[i]; h *= 16777619u; } return h; }
int main(void)
{
    static char line[1 << 20]; static uint8_t blob[1 << 19]; static int32_t out[70000];
    while (fgets(line, sizeof line, stdin)) {
        char *tok = strtok(line, " \n"); uint32_t order = (uint32_t)atoi(tok), bs = (uint32_t)atoi(strtok(0, " \n")); char *hex = strtok(0, " \n");
        uint32_t n = 0; if (hex && strcmp(hex, "-")) for (; hex[2 * n] && hex[2 * n + 1]; n++) { unsigned x; sscanf(hex + 2 * n, "%2x", &x); blob[n] = (uint8_t)x; }
        uint32_t nc = (uint32_t)atoi(strtok(0, " \n")), ch[64], ns, sl[64];
        for (uint32_t i = 0; i < nc; i++) ch[i] = (uint32_t)atoi(strtok(0, " \n"));
        ns = (uint32_t)atoi(strtok(0, " \n")); for (uint32_t i = 0; i < ns; i++) sl[i] = (uint32_t)atoi(strtok(0, " \n"));
        f2_br_t br = {0}; f2_res_t r; uint32_t off = 0, total = 0, ci = 0, si = 0; int status = F2_OK; uint32_t consumed = 0, spins = 0;
        br.p = br.end = blob;
        /* the method/order header, with the same window feeding as the data */
        uint32_t grow = 0;                                                          /* the window the transport presents grows while the decoder keeps asking */
        for (;;) {
            uint32_t len = ch[ci++ % nc] + grow; if (off + len > n) len = n - off;
            br.p = blob + off; br.end = br.p + len;
            status = f2_res_begin(&br, &r, bs, order);
            uint32_t adv = (uint32_t)(br.p - (blob + off)); off += adv;
            if (status != F2_NEED) break;
            if (off >= n && len == 0) break;
            grow = adv ? 0u : len;
            if (++spins > 100000) break;
        }
        if (status == F2_OK) {
            grow = 0;
            for (;;) {
                uint32_t len = ch[ci++ % nc] + grow; if (off + len > n) len = n - off;
                const uint8_t *start = blob + off; br.p = start; br.end = start + len;
                uint32_t want = sl[si++ % ns], done = 0;
                status = f2_res_run(&br, &r, out + total, want, &done);
                total += done; off += (uint32_t)(br.p - start);
                grow = (done || br.p != start) ? 0u : len;
                if (status == F2_ERR) break;
                if (f2_res_done(&r)) { status = F2_OK; break; }
                if (status == F2_NEED && off >= n && len == 0) break;       /* the data really ended */
                if (++spins > 4000000) break;
            }
        }
        consumed = off * 8u - br.nb;
        printf("%d %u %u %u\n", status, total, consumed, fnv(out, total));
        for (uint32_t i = 0; i < total; i++) printf("%d ", out[i]);
        printf("\n");
    }
    return 0;
}
