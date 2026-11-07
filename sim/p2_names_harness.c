/* Host build of fw2/cores/names_core.h (the real firmware code): loads a tau-names.bin and prints what the reader answers.
 *   p2_names_harness FILE BUILD_ID N_TRACKS N_ALBUMS N_ARTISTS [CAP]
 * line 1: "load CODE"; then for every track/album/artist field present: "K id field len hexbytes" (the Latin-1 text names_display wrote, or "-" when it returned 0).
 * sim/test_p2_names_reader.py builds the files with tools/name_sidecar.py and compares with the Python display rule. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../fw2/cores/names_core.h"

static FILE *fp; static uint32_t fsize;
static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx;
    if ((uint64_t)off + len > fsize) return 0;                  /* the card fails a read past the end */
    fseek(fp, (long)off, SEEK_SET);
    return fread(dst, 1, len, fp) == len;
}
static void show(const char *k, uint32_t id, int f, const names_t *n, uint8_t kind, uint32_t cap)
{
    char out[1024]; memset(out, 'Z', sizeof out); out[0] = 'Q'; out[1] = 0;
    uint32_t len = names_display(n, kind, id, (uint8_t)f, out, cap);
    printf("%s %u %d %u ", k, id, f, len);
    if (!len) { printf("-%s\n", out[0] == 'Q' && out[1] == 0 ? "" : " TOUCHED"); return; }
    for (uint32_t i = 0; i < len; i++) printf("%02x", (unsigned char)out[i]);
    printf("%s\n", out[len] == 0 ? "" : " NOTERM");
}
int main(int argc, char **argv)
{
    if (argc < 6) return 2;
    fp = fopen(argv[1], "rb");
    if (!fp) { printf("load %d\n", NAMES_E_NOFILE); return 0; }
    fseek(fp, 0, SEEK_END); fsize = (uint32_t)ftell(fp);
    uint32_t bid = (uint32_t)strtoul(argv[2], 0, 0), nt = (uint32_t)atoi(argv[3]), na = (uint32_t)atoi(argv[4]), nr = (uint32_t)atoi(argv[5]);
    uint32_t cap = argc > 6 ? (uint32_t)atoi(argv[6]) : 512u;
    static uint8_t win[LIB_WIN] __attribute__((aligned(16)));
    static uint8_t mem[NAMES_MAX_FILE] __attribute__((aligned(16)));
    names_t n;
    int e = names_load(&n, rd, 0, win, mem, sizeof mem, bid, nt, na, nr);
    printf("load %d\n", e);
    if (e) return 0;
    for (uint32_t i = 0; i < nt; i++) for (int f = 0; f < 4; f++) if (names_ptr(&n, NAMES_TRACK, i, (uint8_t)f)) show("T", i, f, &n, NAMES_TRACK, cap);
    for (uint32_t i = 0; i < na; i++) for (int f = 0; f < 2; f++) if (names_ptr(&n, NAMES_ALBUM, i, (uint8_t)f)) show("A", i, f, &n, NAMES_ALBUM, cap);
    for (uint32_t i = 0; i < nr; i++) if (names_ptr(&n, NAMES_ARTIST, i, 0)) show("R", i, 0, &n, NAMES_ARTIST, cap);
    printf("ranges %d %d %d\n", names_ptr(&n, NAMES_TRACK, nt, 0) != 0, names_ptr(&n, NAMES_TRACK, 0, 4) != 0, names_ptr(&n, NAMES_ALBUM, 0, 2) != 0);
    return 0;
}
