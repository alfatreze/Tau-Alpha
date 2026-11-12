/* stdin lines: "P meter preset np" then np max, np mv, np base values -> prints "n w0 w1 w2 w3";
 *              "U w0 w1 w2 w3 np" then np max values (the table for any meter) -> prints "n meter preset" and n "idx val" lines (n 0 = refused; "meter" is the 20-bit key).
 *              "A h n h0..hn-1" -> prints tune_key_ambiguous(hashes, n, h): 1 when another of the n id_hashes has the same low 16 bits as h.
 *              "C w0 w1 w2 w3 np" then np max values (the table for any meter) -> tune_convert_v1: prints the four words of layout 2 (the meter index m of a layout-1 record has key m * 0x1111 + 5 for m < 16, else none). */
#include <stdio.h>
#include "tune_core.h"
static uint16_t tmax[64]; static uint32_t tnp;
static const uint16_t *max_of(uint32_t meter, uint32_t *np) { (void)meter; *np = tnp; return tmax; }
static uint32_t key_of(uint32_t m) { return m < 16u ? m * 0x1111u + 5u : TUNE_KEY_NONE; }
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
        } else if (c == 'A') {
            unsigned hh, n, hs[64]; scanf("%u %u", &hh, &n); uint32_t v[64]; for (unsigned i = 0; i < n; i++) { scanf("%u", &hs[i]); v[i] = hs[i]; }
            printf("%u\n", tune_key_ambiguous(v, n, hh));
        } else if (c == 'C') {
            uint32_t w[4], o[4]; unsigned x;
            for (uint32_t i = 0; i < 4; i++) { scanf("%u", &x); w[i] = x; }
            scanf("%u", &np); tnp = np;
            for (uint32_t i = 0; i < np; i++) { scanf("%u", &a); tmax[i] = (uint16_t)a; }
            tune_convert_v1(w, max_of, key_of, o);
            printf("%u %u %u %u\n", o[0], o[1], o[2], o[3]);
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
