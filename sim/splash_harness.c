/* B-574: runs the REAL ui_splash_asset() (fw/player.c, extracted by sim/test_splash_asset.py) on a splash asset file.
 * Stubs: the APF slot read (target_read_slot copies from the asset file into the tag buffer, which the firmware reaches through a raw
 * pointer; the test rewrites that one expression to `tagmem`) and fb_rect(), which paints a 400x360 RGB565 array.
 * Usage: harness <asset> <out.rgb565>   prints "ok=<0|1> pixels=<n>" and writes the framebuffer (little-endian words). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TAU_SPLASH_SLOT_ID/W/H/HEADER/HEADER2/CHUNK come from fw/player.c itself (the test copies those #defines into splash_fw.inc),
 * so the harness cannot drift from the firmware. */
#include "splash_defs.h"
#define TAU_TAG_BYTES       4096u

static uint8_t tagmem[TAU_TAG_BYTES];
static char _tag_start;
static uint8_t *file; static long flen;
static uint16_t fbuf[TAU_SPLASH_W * TAU_SPLASH_H];
static unsigned painted;

static int target_read_slot(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len)
{
    (void)slot; (void)dst_off;
    if (len > TAU_TAG_BYTES || (long)off + (long)len > flen) return 0;
    memcpy(tagmem, file + off, len);
    return 1;
}
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++)
        if (x + i < TAU_SPLASH_W && y + j < TAU_SPLASH_H) { fbuf[(y + j) * TAU_SPLASH_W + x + i] = c; painted++; }
}

#include "splash_fw.inc"

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); flen = ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc((size_t)flen + 1); if (fread(file, 1, (size_t)flen, f) != (size_t)flen) return 2; fclose(f);
    int ok = ui_splash_asset();
    FILE *o = fopen(argv[2], "wb"); fwrite(fbuf, 2, sizeof fbuf / 2, o); fclose(o);
    printf("ok=%d pixels=%u\n", ok, painted);
    return 0;
}
