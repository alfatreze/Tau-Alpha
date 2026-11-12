/* Host check of fw2/cores/meterbin_core.h: for each FILE prints what the firmware's reader makes of it, in the text tools/meter_bin.py report() makes for the same bytes.
 *   h MODE FW_ABI FW_TAU FILE...      MODE file = mb_check_file on the whole file; toc = mb_open_toc on a reader that serves ONLY bytes 0..packs_off (the boot path: a read of a pack or the end CRC fails)
 *                                     MODE count = mb_open_toc only, prints the number of reader calls it made
 *                                     FW_ABI = major << 8 | minor, FW_TAU likewise, decimal
 * per file:  `open CODE COUNT BADROW`, for a sound bin a `row` and an `avail` line per row and a `coll` line (tuning collisions); then a line "--". Every read is bounds checked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/cores/meterbin_core.h"
typedef struct { const uint8_t *d; uint32_t served; } buf_t;
static unsigned long reads;
static int rd(void *c, uint32_t off, uint8_t *dst, uint32_t n)
{
    reads++;
    const buf_t *b = (const buf_t *)c;
    if ((uint64_t)off + n > b->served) return 0;
    memcpy(dst, b->d + off, n); return 1;
}
int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    const int cnt = !strcmp(argv[1], "count"), toc = cnt || !strcmp(argv[1], "toc");
    const uint32_t abi = (uint32_t)atoi(argv[2]), tau = (uint32_t)atoi(argv[3]);
    static mb_info_t in;
    for (int a = 4; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) return 2;
        fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *d = (uint8_t *)malloc((size_t)n + 1); if ((long)fread(d, 1, (size_t)n, f) != n) return 2; fclose(f);
        buf_t b = { d, (uint32_t)n };
        if (toc && n >= 40 && mb_ld16(d + 4) == 2u) { const uint32_t po = mb_ld32(d + 24); if (po <= (uint32_t)n) b.served = po; }
        const int e = toc ? mb_open_toc(rd, &b, (uint32_t)n, &in) : mb_check_file(rd, &b, (uint32_t)n, &in);
        if (cnt) { printf("reads %lu\n", reads); free(d); continue; }
        if (e) printf("open %d 0 %d\n", e, in.bad_row);
        else {
            printf("open 0 %u -1\n", in.count);
            b.served = (uint32_t)n;
            if (toc) b.served = in.packs_off;
            uint32_t h[MB_MAX_METERS]; uint8_t fl[MB_MAX_METERS];
            for (uint32_t i = 0; i < in.count; i++) {
                mb_row_t r;
                if (mb_row(rd, &b, &in, i, &r)) { printf("row %u FAILED\n", i); continue; }
                printf("row %u %u %s %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %s\n", i, r.id_hash, r.id[0] ? r.id : "-", r.ver[0], r.ver[1], r.ver[2], r.abi, r.tau_min, r.flags, r.params_rev, r.nparams, r.cmds,
                       r.max_cycles, r.pack_off, r.pack_size, r.pack_crc, r.pstate, r.bss, r.preview_off, r.preview_len, r.name);
            }
            for (uint32_t i = 0; i < in.count; i++) { mb_row_t r; mb_row(rd, &b, &in, i, &r); printf("avail %u %d\n", i, mb_avail(&r, abi, tau)); }
            mb_hashes(rd, &b, &in, h); mb_tune_collisions(h, in.count, fl);
            printf("coll"); for (uint32_t i = 0; i < in.count; i++) printf(" %d", fl[i]); printf("\n");
        }
        printf("--\n");
        free(d);
    }
    return 0;
}
