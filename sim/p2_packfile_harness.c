/* Host check of fw2/stream/packfile.c: the real packfile_rd over a stub slot driver that counts reads. Commands (stdin): "file PATH", "load OFF SIZE" (the reads of loading a pack of SIZE bytes at OFF the way
 * mpk_install does: the header, the header again, the last byte, then the image in 4 KB pieces; prints the slot reads), "failat N" (the Nth slot read fails), "big" (one 20000-byte read). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define CLK_HZ 66666667u
static uint8_t tag[4096]; char _tag_start;
#define PACKFILE_TAGWIN ((const volatile uint8_t *)tag)
static uint8_t *file; static uint32_t flen, reads, maxn, last_to, fail_at, failed;
int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst, uint32_t n, uint32_t to) { (void)slot; (void)dst; reads++; if (n > maxn) maxn = n; last_to = to; if (fail_at && reads == fail_at) { failed++; return 0; } if (n > sizeof tag || (uint64_t)off + n > flen) return 0; memcpy(tag, file + off, n); return 1; }
#include "../fw2/stream/packfile.c"
int main(void)
{
    char a[16], b[300]; unsigned x, y; static uint8_t buf[70000];
    while (scanf("%15s", a) == 1) {
        if (!strcmp(a, "file")) { scanf("%299s", b); FILE *f = fopen(b, "rb"); fseek(f, 0, SEEK_END); flen = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET); file = malloc(flen); if (fread(file, 1, flen, f) != flen) return 2; fclose(f); }
        else if (!strcmp(a, "failat")) { scanf("%u", &x); fail_at = x; }
        else if (!strcmp(a, "load")) { scanf("%u %u", &x, &y); reads = 0; int ok = packfile_rd((void *)1, x, buf, 48u) && packfile_rd((void *)1, x, buf, 48u) && packfile_rd((void *)1, x + 48u + y - 1u, buf, 1u);
            for (uint32_t off = 0; ok && off < y; ) { uint32_t n = y - off; if (n > 4096u) n = 4096u; ok = packfile_rd((void *)1, x + 48u + off, buf, n); off += n; }
            printf("load ok %d reads %u maxn %u timeout %u failed %u\n", ok, reads, maxn, last_to, failed); }
        else if (!strcmp(a, "big")) { reads = 0; int ok = packfile_rd((void *)2, 0, buf, 20000u); printf("big ok %d reads %u maxn %u\n", ok, reads, maxn); }
    }
    return 0;
}
