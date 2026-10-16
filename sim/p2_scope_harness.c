/* Host check of fw2/ui/meter_winamp_scope.c against a model of the capture block: after every drawn frame the picture equals a clean render of the module's own state AND nothing outside the box changed (the old
 * player's corruption was a trace escaping its box). The captures include DC-biased and one-sided windows. Prints MISMATCH n OUTSIDE n FRAMES n. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/ui/meter_winamp_scope.c"
#define W 400u
#define H 360u
static uint16_t fb[W * H], ex[W * H];
uint16_t th_role[TR_COUNT];
static uint32_t idx_sel, cap_busy, arms; static int16_t cmin[256], cmax[256];
uint32_t host_rd(uint32_t a)
{
    if (a == 0x800000FCu) return 1u | (cap_busy ? 2u : 0u);
    if (a == 0x800000F4u) return ((uint32_t)(uint16_t)cmin[idx_sel & 255u] << 16) | (uint16_t)cmax[idx_sel & 255u];
    return 0;
}
void host_wr(uint32_t a, uint32_t v) { if (a == 0x800000F0u) idx_sel = v; else if (a == 0x800000ECu && (v & 2u)) arms++; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) fb[(y + j) * W + x + i] = c; }
static void rect_to(uint16_t *e, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) e[(y + j) * W + x + i] = c; }
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(int argc, char **argv)
{
    rng = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u; int steps = argc > 2 ? atoi(argv[2]) : 400;
    th_role[TR_ACCENT] = 0xFD05; th_role[TR_SURFACE_TRACK] = 0x18E3;
    const uint16_t BGC = 0x0000; uint8_t spec[MTR_BANDS] = { 0 };
    const uint32_t BX = 20, BY = 100, BW = 360, BH = 120;
    int bad = 0, outside = 0, frames = 0;
    for (uint32_t i = 0; i < W * H; i++) fb[i] = 0x7777;                       /* a guard colour everywhere */
    for (int n = 0; n < steps; n++) {
        const int mode = (int)(rnd() % 4);                                     /* 0 normal, 1 DC-biased high, 2 one-sided, 3 loud */
        for (uint32_t c = 0; c < 256u; c++) {
            int32_t m = (int32_t)(rnd() % (mode == 3 ? 32000u : 6000u)), a = -(int32_t)(rnd() % (mode == 3 ? 32000u : 6000u));
            if (mode == 1) { a += 12000; m += 12000; }
            if (mode == 2) { a = 0; }
            cmin[c] = (int16_t)(a < -32768 ? -32768 : a); cmax[c] = (int16_t)(m > 32767 ? 32767 : m);
        }
        cap_busy = (n % 5 == 3);
        mtr_in_t in = { spec, (uint32_t)n, 26, (uint16_t)BX, (uint16_t)BY, (uint16_t)BW, (uint16_t)BH, BGC, (uint8_t)(n == 0), (uint8_t)(n % 97 > 90), 0, 0 };
        uint16_t before[W * H]; memcpy(before, fb, sizeof fb);
        meter_winamp_scope_tick(&in);
        if (n == 0) continue;                                                   /* the forced first call only clears the box and arms the capture */
        if (memcmp(before, fb, sizeof fb) == 0) continue;                       /* nothing drawn this call (arming, busy, paused) */
        frames++;
        for (uint32_t y = 0; y < H; y++) for (uint32_t x = 0; x < W; x++)
            if ((x < BX || x >= BX + BW || y < BY || y >= BY + BH) && fb[y * W + x] != 0x7777) { outside++; goto chk; }
    chk:
        for (uint32_t i = 0; i < W * H; i++) ex[i] = 0x7777;
        rect_to(ex, BX, BY, BW, BH, BGC); rect_to(ex, BX, BY + BH / 2, BW, 1, 0x18E3);
        const int32_t cy = (int32_t)(BY + BH / 2);
        for (uint32_t c = 0; c < COLS; c++) {
            const uint32_t cx = BX + (c * BW) / COLS, cxn = BX + ((c + 1u) * BW) / COLS, cwid = cxn > cx ? cxn - cx : 1u;
            int32_t s_top = cy - (sc_mid[c] + sc_half[c]), s_bot = s_top + 2 * sc_half[c] + 2;
            if (s_top < (int32_t)BY) s_top = (int32_t)BY;
            if (s_bot > (int32_t)(BY + BH)) s_bot = (int32_t)(BY + BH);
            if (s_bot < s_top) s_bot = s_top;
            rect_to(ex, cx, (uint32_t)s_top, cwid, (uint32_t)(s_bot - s_top), 0xFD05);
        }
        for (uint32_t y = BY; y < BY + BH; y++) for (uint32_t x = BX; x < BX + BW; x++) if (fb[y * W + x] != ex[y * W + x]) { bad++; goto next; }
    next:;
    }
    printf("MISMATCH %d OUTSIDE %d FRAMES %d\n", bad, outside, frames);
    return 0;
}
