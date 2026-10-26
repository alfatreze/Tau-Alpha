/* Host harness for fw/tpg.h: prints the block (x0, y0, px) and then the whole 400x360 image (0 outside the block) for a record read from a
 * file (sim/test_tpg_fw.py compares it with tools/tpg.py pixel for pixel). */
#include <stdio.h>
#include <stdlib.h>
#include "tpg.h"
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    static uint8_t rec[300000]; uint32_t len = (uint32_t)fread(rec, 1, sizeof rec, f); fclose(f);
    uint32_t mode = (uint32_t)atoi(argv[2]);
    tpg_t t;
    if (!tpg_init(&t, rec, len, mode)) { printf("REFUSED\n"); return 0; }
    uint32_t blk[3] = { t.x0, t.y0, t.px };
    fwrite(blk, 4, 3, stdout);
    for (uint32_t y = 0; y < TPG_H; y++)
        for (uint32_t x = 0; x < TPG_W; x++) {
            uint16_t v = (x >= t.x0 && x < t.x0 + t.px && y >= t.y0 && y < t.y0 + t.px) ? tpg_pixel(&t, x, y) : 0u;
            fwrite(&v, 2, 1, stdout);
        }
    return 0;
}
