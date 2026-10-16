/* stdin lines: "P meter preset np" then np max, np mv, np base values -> prints "n w0 w1 w2 w3";
 *              "U w0 w1 w2 w3 np" then np max values (the table for any meter) -> prints "n meter preset" and n "idx val" lines (n 0 = refused). */
#include <stdio.h>
#include "tune_core.h"
static uint16_t tmax[64]; static uint32_t tnp;
static const uint16_t *max_of(uint32_t meter, uint32_t *np) { (void)meter; *np = tnp; return tmax; }
int main(void)
{
    char c; unsigned a, m, p, np;
    while (scanf(" %c", &c) == 1) {
        if (c == 'P') {
            uint16_t mx[64], mv[64], bs[64]; uint32_t w[4];
            scanf("%u %u %u", &m, &p, &np);
            for (uint32_t i = 0; i < np; i++) { scanf("%u", &a); mx[i] = (uint16_t)a; }
            for (uint32_t i = 0; i < np; i++) { scanf("%u", &a); mv[i] = (uint16_t)a; }
            for (uint32_t i = 0; i < np; i++) { scanf("%u", &a); bs[i] = (uint16_t)a; }
            uint32_t n = tune_pack(m, p, mv, bs, mx, np, w);
            printf("%u %u %u %u %u\n", n, w[0], w[1], w[2], w[3]);
        } else {
            uint32_t w[4]; unsigned x;
            for (uint32_t i = 0; i < 4; i++) { scanf("%u", &x); w[i] = x; }
            scanf("%u", &np); tnp = np;
            for (uint32_t i = 0; i < np; i++) { scanf("%u", &a); tmax[i] = (uint16_t)a; }
            uint32_t mt = 0, pr = 0; uint8_t ix[TUNE_MAX]; uint16_t vl[TUNE_MAX];
            uint32_t n = tune_unpack(w, max_of, &mt, &pr, ix, vl);
            printf("%u %u %u\n", n, n ? mt : 0u, n ? pr : 0u);
            for (uint32_t k = 0; k < n; k++) printf("%u %u\n", ix[k], vl[k]);
        }
    }
    return 0;
}
