/* Host check of fw2/ui/meterlist_thumb.c: the stash plane the module builds from each meter's run-length stream, and the palette and blit it issues. Prints, per meter, the plane rows as one hex string, then the palette and the blit. */
#include <stdio.h>
#include <stdint.h>
#include "../fw2/ui/meterlist_thumb.c"
uint16_t th_role[TR_COUNT];
static uint8_t plane[32][56 * 8]; static uint32_t cnt, pal[8], npal; static int bad;
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { if (h != 1 || y < THUMB_PLANE_Y || y >= THUMB_PLANE_Y + 32u || x + w > 56u * 8u) { bad = 1; return; } for (uint32_t i = 0; i < w; i++) plane[y - THUMB_PLANE_Y][x + i] = (uint8_t)c; }
void gfx_clut_begin(uint32_t s) { if (s != 0) bad = 1; npal = 0; }
void gfx_clut_put(uint16_t c) { pal[npal++ & 7u] = c; }
static uint32_t bsx, bsy, bdx, bdy, bw, bh;
void gfx_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { bsx = sx; bsy = sy; bdx = dx; bdy = dy; bw = w; bh = h; cnt++; }
int main(void)
{
    for (uint32_t i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)((i * 0x0841u + 0x1234u) & 0xFFFFu);          /* distinct, known role colours (the test computes the expected palette from them) */
    for (uint32_t m = 0; m < METER_N; m++) {
        meter_thumb_draw(m, 20u + m, 50u + m); meter_thumb_draw(m, 20u + m, 50u + m);       /* twice: the plane is built once */
        printf("m%u ", m);
        for (uint32_t y = 0; y < 32u; y++) for (uint32_t x = 0; x < 56u; x++) printf("%u", plane[y][m * 56u + x]);
        printf(" pal"); for (uint32_t c = 0; c < 8u; c++) printf(" %u", pal[c]);
        printf(" blit %u %u %u %u %u %u\n", bsx, bsy, bdx, bdy, bw, bh);
    }
    printf("calls %u bad %d\n", cnt, bad);
    return 0;
}
