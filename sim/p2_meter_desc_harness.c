/* Host harness for fw2/cores/meter_desc_core.h: reads a descriptor file (argv[1]), checks it (and counts any read outside the descriptor) and prints it in a form the Python reader can compare. */
#include <stdio.h>
#include <stdlib.h>
#include "meter_desc_core.h"
static uint8_t blob[8192]; static uint32_t blen; static int outside;
static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { (void)ctx; if (off > blen || n > blen - off) { outside++; return 0; } for (uint32_t i = 0; i < n; i++) dst[i] = blob[off + i]; return 1; }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2; blen = (uint32_t)fread(blob, 1, sizeof blob, f); fclose(f);
    if (argc > 2 && argv[2][0] == 'f') {                                                   /* fuzz: every single bit flipped in turn: no read may leave the descriptor */
        uint32_t accepted = 0, refused = 0;
        for (uint32_t i = 0; i < blen * 8u; i++) { blob[i >> 3] ^= (uint8_t)(1u << (i & 7u)); md_hdr_t hh; if (md_check(rd, 0, blen, 21u, &hh) == 0) accepted++; else refused++; blob[i >> 3] ^= (uint8_t)(1u << (i & 7u)); }
        printf("fuzz accepted %u refused %u outside %d\n", accepted, refused, outside); return 0;
    }
    uint32_t len = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 10) : blen;
    md_hdr_t h; const int e = md_check(rd, 0, len, 21u, &h);
    printf("check %d outside %d\n", e, outside);
    if (e) return 0;
    char s[32]; md_string(rd, 0, &h, h.name, s, sizeof s); printf("name %s cmds %u cmp %u maxc %u np %u npre %u\n", s, h.cmds, h.cmp, h.max_cycles, h.np, h.npre);
    for (uint32_t i = 0; i < h.np; i++) {
        md_param_t p; md_param(rd, 0, &h, i, &p); md_string(rd, 0, &h, p.label, s, sizeof s);
        printf("p %s %u %u %u %u %u %u %u", s, p.type, p.hidden, p.min, p.max, p.step, p.when_idx, p.when_val);
        for (uint32_t j = 0; j < p.nval; j++) { uint32_t o; md_val_off(rd, 0, &p, j, &o); md_string(rd, 0, &h, o, s, sizeof s); printf(" [%s]", s); }
        printf("\n");
    }
    for (uint32_t k = 0; k < h.npre; k++) { uint32_t no; uint16_t v[MD_MAX_PARAMS]; md_preset(rd, 0, &h, k, &no, v); md_string(rd, 0, &h, no, s, sizeof s); printf("r %s", s); for (uint32_t i = 0; i < h.np; i++) printf(" %u", v[i]); printf("\n"); }
    for (uint32_t i = 0; i < 8u; i++) { md_col_t c; md_col(rd, 0, &h, i, &c); printf("c %u %u %u %u %u\n", c.fixed, c.a, c.b, c.t, c.n); }
    printf("rle"); for (uint32_t i = 0; i < h.rle_len; i++) { uint8_t v; md_rle_byte(rd, 0, &h, i, &v); printf(" %u", v); } printf("\n");
    return 0;
}
