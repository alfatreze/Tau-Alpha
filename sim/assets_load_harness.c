/* B-628: drives fw/assets_core.h's as_load() with a fake reader over a file. argv: file, fail_at_call (0 = never), scratch_cap (0 = AS_MAX_FILE), prove (1 ok / 0 fails).
 * Prints "code total same" where same = the bytes at base equal the file's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets_core.h"
static uint8_t *file; static uint32_t flen; static int calls, fail_at, nofile;
static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx;
    calls++;
    if (nofile || (fail_at && calls == fail_at)) return 0;
    if (off + len > flen) return 0;
    memcpy(dst, file + off, len);
    return 1;
}
static int prove_ok(void) { return 1; }
static int prove_bad(void) { return 0; }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) { nofile = 1; flen = 0; file = 0; } else { fseek(f, 0, SEEK_END); flen = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET); file = malloc(flen ? flen : 1); fread(file, 1, flen, f); fclose(f); }
    fail_at = atoi(argv[2]);
    uint32_t cap = (uint32_t)atoi(argv[3]); if (!cap) cap = AS_MAX_FILE;
    int prove = atoi(argv[4]);
    static uint8_t win[LIB_WIN] __attribute__((aligned(4)));
    static uint8_t scratch[AS_MAX_FILE + 8] __attribute__((aligned(4)));
    memset(scratch, 0xEE, sizeof scratch);
    const uint8_t *base = 0; uint32_t total = 0;
    int e = as_load(rd, 0, win, scratch, cap, prove ? prove_ok : prove_bad, &base, &total);
    int same = (e == AS_OK) && total <= flen && memcmp(base, file, total) == 0;
    printf("%d %u %d %s\n", e, e == AS_OK ? total : 0u, same, (e == AS_OK && base == win) ? "win" : "scratch");
    return 0;
}
