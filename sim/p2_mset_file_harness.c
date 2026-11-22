/* Host harness for fw2/cores/mset_file_core.h: the firmware's save-file code against a RAM model of the 2,048-word save RAM. usage:
 *   harness <file.bin|-> <out.bin> <tear_after_writes|-1> put <id_hash> <rev> <preset> <idx:val,idx:val...|->     one edit (an empty list removes the record)
 *   harness <file.bin|-> <out.bin> -1 find <id_hash> <rev>                                                        prints "found=<0|1> rev_ok=<0|1> preset= n= idx:val,..."
 *   harness <file.bin|-> <out.bin> -1 scan                                                                        prints "active= locked= valid0= valid1= seq0= seq1= count0= count1="
 * tear_after_writes models a Quit in the middle of the update: later writes are dropped. A word read is checked: the index must stay inside the RAM. sim/test_p2_mset_file.py drives it.
 * -DBUG=n (fw2/cores/mset_file_core.h): 1 = the tail of the bank is not zero-filled, 2 = the CRC is not inverted, 3 = the valid generation is written in place (same bank), 4 = no eviction. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define MF_FN static
#include "../fw2/cores/mset_file_core.h"
#define WORDS 2048u
static uint32_t mem[WORDS]; static uint32_t idx_; static long writes, tear = -1;
static void io_seek(uint32_t w) { if (w >= WORDS) { fprintf(stderr, "seek out of range %u\n", w); exit(9); } idx_ = w; }
static uint32_t io_rd(void) { if (idx_ >= WORDS) { fprintf(stderr, "read out of range\n"); exit(9); } return mem[idx_++]; }
static void io_wr(uint32_t v) { if (idx_ >= WORDS) { fprintf(stderr, "write out of range\n"); exit(9); } if (tear < 0 || writes < tear) mem[idx_] = v; writes++; idx_++; }
int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    memset(mem, 0, sizeof mem);
    if (strcmp(argv[1], "-")) { FILE *f = fopen(argv[1], "rb"); if (!f) return 3; uint8_t b[WORDS * 4]; if (fread(b, 1, sizeof b, f) != sizeof b) return 3; fclose(f);
        for (uint32_t i = 0; i < WORDS; i++) mem[i] = b[4*i] | b[4*i+1] << 8 | b[4*i+2] << 16 | (uint32_t)b[4*i+3] << 24; }
    tear = atol(argv[3]);
    const mf_io_t io = { io_seek, io_rd, io_wr };
    if (!strcmp(argv[4], "put") && argc >= 9) {
        uint8_t ix[MF_MAXN]; uint16_t vl[MF_MAXN]; uint32_t n = 0;
        if (strcmp(argv[8], "-")) { char *s = argv[8]; while (*s && n < MF_MAXN) { ix[n] = (uint8_t)strtoul(s, &s, 0); if (*s == ':') s++; vl[n] = (uint16_t)strtoul(s, &s, 0); n++; if (*s == ',') s++; } }
        const int ok = mf_put(&io, (uint32_t)strtoul(argv[5], 0, 0), (uint32_t)strtoul(argv[6], 0, 0), (uint32_t)strtoul(argv[7], 0, 0), n, ix, vl);
        printf("ok=%d\n", ok);
    } else if (!strcmp(argv[4], "find") && argc >= 7) {
        mf_scan_t s; mf_scan(&io, &s); mf_rec_t r; int rev_ok = 0;
        const int f = mf_find(&io, &s, (uint32_t)strtoul(argv[5], 0, 0), (uint32_t)strtoul(argv[6], 0, 0), &r, &rev_ok);
        printf("found=%d rev_ok=%d preset=%u n=%u", f, rev_ok, f ? r.preset : 0, f ? r.n : 0);
        for (uint32_t k = 0; f && k < r.n; k++) printf("%c%u:%u", k ? ',' : ' ', r.idx[k], r.val[k]);
        printf("\n");
    } else if (!strcmp(argv[4], "scan")) {
        mf_scan_t s; mf_scan(&io, &s);
        printf("active=%d locked=%d valid0=%u valid1=%u seq0=%u seq1=%u count0=%u count1=%u\n", s.active, s.locked, s.g[0].valid, s.g[1].valid, s.g[0].seq, s.g[1].seq, s.g[0].count, s.g[1].count);
    } else return 2;
    FILE *o = fopen(argv[2], "wb"); uint8_t b[WORDS * 4];
    for (uint32_t i = 0; i < WORDS; i++) { b[4*i] = mem[i]; b[4*i+1] = mem[i] >> 8; b[4*i+2] = mem[i] >> 16; b[4*i+3] = mem[i] >> 24; }
    fwrite(b, 1, sizeof b, o); fclose(o);
    return 0;
}
