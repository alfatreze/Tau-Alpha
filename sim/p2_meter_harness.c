/* Host check of fw2/ui/meter.c: after EVERY meter_draw() the box on the "screen" must equal a clean render of the meter's own state (bar heights px[], caps cap_px[]). gfx_rect paints into a
 * model of the framebuffer. The stimulus is bursts of loud and quiet windows so bars fall while their caps are still held (the "full bar that shrinks to a bit on top" bug).
 * Args: seed steps. Prints MISMATCH n (frames whose picture differed from the clean render) and DRAWN n. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/ui/meter.c"
#include "../fw2/platform/platform.h"
caps_t caps;
#define W 400u
#define H 360u
static uint16_t fb[W * H];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) { if (x + i < W && y + j < H) fb[(y + j) * W + x + i] = color; }
}
static uint32_t win, means[16], idx_sel;
uint32_t host_rd(uint32_t a)
{
    if (a == 0x800000E4u) return (win & 0xFFFFu) << 16 | 1u;
    if (a == 0x800000E0u) return means[idx_sel];
    return 0;
}
void host_wr(uint32_t a, uint32_t v) { if (a == 0x800000DCu) idx_sel = v & 15u; }

static void gfx_rect_to(uint16_t *e, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) { if (x + i < W && y + j < H) e[(y + j) * W + x + i] = color; }
}
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static void expected(uint16_t *e)
{
    for (uint32_t i = 0; i < W * H; i++) e[i] = BG;
    for (uint32_t b = 0; b < SPEC_BANDS; b++) {
        uint32_t x = X0 + b * (BAR_W + GAP), base = box_y + box_h - 2u;
        if (px[b]) gfx_rect_to(e, x, base - px[b], BAR_W, px[b], px[b] > (box_h * 3u) / 4u ? HOT : BAR);
        if (cap_px[b] > px[b] + 1u) gfx_rect_to(e, x, base - cap_px[b] - 2u, BAR_W, 2u, CAP);
    }
}
int main(int argc, char **argv)
{
    rng = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u; int steps = argc > 2 ? atoi(argv[2]) : 2000;
    caps.spec = 1; for (uint32_t i = 0; i < W * H; i++) fb[i] = BG;
    meter_enter(212, 80);
    static uint16_t e[W * H]; int bad = 0, drawn = 0, loud = 0;
    win = 1;
    for (int n = 0; n < steps; n++) {
        if (n % 40 == 0) loud = (int)(rnd() % 3);                      /* 0 quiet, 1 medium, 2 loud, for 40 windows */
        for (uint32_t b = 0; b < 16; b++) {
            uint32_t base = loud == 2 ? 3000000u : loud == 1 ? 40000u : 300u;
            means[b] = (rnd() % (base + 1u)) & 0xFFFFFu;
            if (n % 40 == 0) means[b] = loud == 2 ? 0xFFFFFu : means[b];
        }
        win++;
        meter_draw(1);
        expected(e);
        drawn++;
        if (memcmp(fb, e, sizeof fb) != 0) bad++;
    }
    printf("MISMATCH %d DRAWN %d\n", bad, drawn);
    return 0;
}
