/* Host harness for fw/tpg.h: prints rows, then every pixel of the stream, for a record read from a file (sim/test_tpg_fw.py). */
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
    uint32_t rows = tpg_rows(&t);
    fwrite(&rows, 4, 1, stdout);
    for (uint32_t y = 0; y < TPG_H; y++) for (uint32_t x = 0; x < TPG_W; x++) { uint16_t v = tpg_pixel(&t, x, y); fwrite(&v, 2, 1, stdout); }
    return 0;
}
