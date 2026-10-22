/* Host check of fw2/ui/meterlist_thumb.c: the stash plane the module builds from each meter's run-length stream, and the palette and blit it issues. Prints, per meter, the plane rows as one hex string, then the palette and the blit. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "../fw2/cores/meter_desc_core.h"
#include "../fw2/ui/meterlist_thumb.c"
uint16_t th_role[TR_COUNT];
/* the pack-only row (row METER_N, the Waterfall): its preview comes from the descriptor in argv[1], read through the same core the firmware uses */
static uint8_t desc[4096]; static uint32_t dlen;
static int drd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { (void)ctx; if (off > dlen || n > dlen - off) return 0; for (uint32_t i = 0; i < n; i++) dst[i] = desc[off + i]; return 1; }
int meters_present(uint32_t m) { return m <= METER_N && (m < METER_N || dlen); }
uint32_t meters_thumb_len(uint32_t m) { md_hdr_t h; return m == METER_N && md_header(drd, 0, dlen, &h) ? h.rle_len : 0u; }
int meters_thumb_rle(uint32_t m, uint32_t i, uint8_t *v) { md_hdr_t h; return m == METER_N && md_header(drd, 0, dlen, &h) && md_rle_byte(drd, 0, &h, i, v); }
int meters_thumb_col(uint32_t m, uint32_t c, uint16_t *fixed, uint8_t *a, uint8_t *b, uint8_t *t, uint8_t *n) { md_hdr_t h; md_col_t e; if (m != METER_N || !md_header(drd, 0, dlen, &h) || !md_col(drd, 0, &h, c, &e)) return 0; *fixed = e.fixed; *a = e.a; *b = e.b; *t = e.t; *n = e.n; return 1; }
static uint8_t plane[32][56 * 8]; static uint32_t cnt, pal[8], npal; static int bad;
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { if (h != 1 || y < THUMB_PLANE_Y || y >= THUMB_PLANE_Y + 32u || x + w > 56u * 8u) { bad = 1; return; } for (uint32_t i = 0; i < w; i++) plane[y - THUMB_PLANE_Y][x + i] = (uint8_t)c; }
void gfx_clut_begin(uint32_t s) { if (s != 0) bad = 1; npal = 0; }
void gfx_clut_put(uint16_t c) { pal[npal++ & 7u] = c; }
static uint32_t bsx, bsy, bdx, bdy, bw, bh;
void gfx_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { bsx = sx; bsy = sy; bdx = dx; bdy = dy; bw = w; bh = h; cnt++; }
int main(int argc, char **argv)
{
    if (argc > 1) { FILE *f = fopen(argv[1], "rb"); dlen = f ? (uint32_t)fread(desc, 1, sizeof desc, f) : 0u; if (f) fclose(f); }
    for (uint32_t i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)((i * 0x0841u + 0x1234u) & 0xFFFFu);          /* distinct, known role colours (the test computes the expected palette from them) */
    for (uint32_t m = 0; m < METER_SLOTS; m++) {
        if (!meters_present(m)) continue;
        meter_thumb_draw(m, 20u + m, 50u + m); meter_thumb_draw(m, 20u + m, 50u + m);       /* twice: the plane is built once */
        printf("m%u ", m);
        for (uint32_t y = 0; y < 32u; y++) for (uint32_t x = 0; x < 56u; x++) printf("%u", plane[y][m * 56u + x]);
        printf(" pal"); for (uint32_t c = 0; c < 8u; c++) printf(" %u", pal[c]);
        printf(" blit %u %u %u %u %u %u\n", bsx, bsy, bdx, bdy, bw, bh);
    }
    printf("calls %u bad %d\n", cnt, bad);
    return 0;
}
