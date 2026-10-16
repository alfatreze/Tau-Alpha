/* Host check of fw2/ui/meter_winamp_bars.c: after EVERY tick the picture equals a clean render of the module's own state (band heights disp[], caps pk[].peak). gfx_rect paints a framebuffer model. The
 * stimulus is loud and quiet bursts (bars fall under held caps), pauses and forced repaints. Prints MISMATCH n and TICKS n. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/ui/meter_winamp_bars.c"
#define W 400u
#define H 360u
static uint16_t fb[W * H], ex[W * H];
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) fb[(y + j) * W + x + i] = c; }
static void rect_to(uint16_t *e, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) e[(y + j) * W + x + i] = c; }
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(int argc, char **argv)
{
    rng = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u; int steps = argc > 2 ? atoi(argv[2]) : 1500;
    th_role[TR_ACCENT] = 0xFD05; th_role[TR_TEXT_PRIMARY] = 0xFFFF;
    const uint16_t BGC = 0x0000; uint8_t spec[MTR_BANDS]; int bad = 0, loud = 0;
    for (uint32_t i = 0; i < W * H; i++) fb[i] = BGC;
    for (int n = 0; n < steps; n++) {
        if (n % 30 == 0) loud = (int)(rnd() % 3);
        for (uint32_t b = 0; b < MTR_BANDS; b++) spec[b] = (uint8_t)(loud == 2 ? 180 + rnd() % 76 : loud == 1 ? rnd() % 120 : rnd() % 12);
        mtr_in_t in = { spec, (uint32_t)n, 26, 20, 212, 360, 80, BGC, (uint8_t)(n == 0 || n % 211 == 0), (uint8_t)(n % 307 > 290), 0, 0 };
        if (in.force) for (uint32_t i = 0; i < W * H; i++) fb[i] = (rnd() & 1) ? BGC : 0x1234;      /* stale pixels the forced wipe must clear */
        meter_winamp_bars_tick(&in);
        for (uint32_t i = 0; i < W * H; i++) ex[i] = BGC;
        const uint32_t gap = 2u, colw = (360u - gap * 15u) / 16u;
        for (uint32_t b = 0; b < 16u; b++) {
            const uint32_t x = 20u + b * (colw + gap); uint32_t bh = (disp[b] * 80u) / 255u; if (bh < 2u) bh = 2u;
            rect_to(ex, x, 212u + 80u - bh, colw, bh, 0xFD05);
            const uint32_t ph = (pk[b].peak * 80u) / 255u;
            if (ph > bh + 1u && ph < 80u) rect_to(ex, x, 212u + 80u - ph, colw, 1u, 0xFFFF);
        }
        for (uint32_t y = 212; y < 292; y++) for (uint32_t x = 20; x < 380; x++) if (fb[y * W + x] != ex[y * W + x]) { bad++; goto next; }
    next:;
    }
    printf("MISMATCH %d TICKS %d\n", bad, steps);
    return 0;
}
