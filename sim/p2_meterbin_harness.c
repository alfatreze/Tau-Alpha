/* Host check of fw2/cores/meterbin_core.h: for each FILE prints what the firmware's reader makes of it, in the text tools/meter_bin.py report() makes for the same bytes.
 *   h FW_ABI FW_TAU FILE...      (FW_ABI = major << 8 | minor, FW_TAU likewise, decimal)
 * per file:  "open CODE COUNT BADROW", then for a sound bin a `row` line and an `avail` line per row; then a line "--". Every read is bounds checked: one outside the file fails (the core must never ask for one). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/cores/meterbin_core.h"
typedef struct { const uint8_t *d; uint32_t n; } buf_t;
static int rd(void *c, uint32_t off, uint8_t *dst, uint32_t n)
{
    const buf_t *b = (const buf_t *)c;
    if ((uint64_t)off + n > b->n) return 0;
    memcpy(dst, b->d + off, n); return 1;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const uint32_t abi = (uint32_t)atoi(argv[1]), tau = (uint32_t)atoi(argv[2]);
    static mb_info_t in;
    for (int a = 3; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) return 2;
        fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *d = (uint8_t *)malloc((size_t)n + 1); if ((long)fread(d, 1, (size_t)n, f) != n) return 2; fclose(f);
        buf_t b = { d, (uint32_t)n };
        const int e = mb_open(rd, &b, (uint32_t)n, &in);
        if (e) printf("open %d 0 %d\n", e, in.bad_row);
        else {
            printf("open 0 %u -1\n", in.count);
            for (uint32_t i = 0; i < in.count; i++) {
                mb_row_t r;
                if (mb_row(rd, &b, &in, i, &r)) { printf("row %u FAILED\n", i); continue; }
                printf("row %u %u %s %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %s\n", i, r.id_hash, r.id[0] ? r.id : "-", r.ver[0], r.ver[1], r.ver[2], r.abi, r.tau_min, r.flags, r.params_rev, r.nparams, r.cmds,
                       r.max_cycles, r.pack_off, r.pack_size, r.pack_crc, r.pstate, r.bss, r.preview_off, r.preview_len, r.name);
            }
            for (uint32_t i = 0; i < in.count; i++) { mb_row_t r; mb_row(rd, &b, &in, i, &r); printf("avail %u %d\n", i, mb_avail(&r, abi, tau)); }
        }
        printf("--\n");
        free(d);
    }
    return 0;
}
