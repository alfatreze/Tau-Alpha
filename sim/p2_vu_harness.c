/* Host check of fw2/ui/meter_vu_master.c: after every tick the ladders equal a clean render of the module's own state (the eased index of each channel and the held peak): a background row, the lit run as
 * one rectangle per colour zone, the held-peak marker above the lit run. Loud/quiet bursts, pauses, forced repaints. Prints MISMATCH n TICKS n. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fw2/ui/meter_vu_master.c"
#define W 400u
#define H 360u
static uint16_t fb[W * H], ex[W * H];
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) fb[(y + j) * W + x + i] = c; }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { (void)x; (void)y; (void)s; (void)fg; (void)bg; return 0; }
static void rect_to(uint16_t *e, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) e[(y + j) * W + x + i] = c; }
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static void expect_ladder(uint32_t ch, uint32_t lx, uint32_t ly, uint32_t lw, uint16_t bg)
{
    rect_to(ex, lx, ly, lw, ROW_H, bg);
    const uint32_t lit = vum_peak_to_segments((uint32_t)disp[ch] << 7, vu_db_to_segment, VU_SEGMENT_TABLE_N);
    static const uint32_t zlo[3] = { 1, 17, 22 }, zhi[3] = { 16, 21, 24 };
    for (uint32_t z = 0; z < 3u; z++) {                                        /* the lit segments of the zone, as one run */
        if (lit < zlo[z]) continue;
        const uint32_t last = lit < zhi[z] ? lit : zhi[z];
        uint32_t sw0, sw1; const uint32_t x0 = seg_x(lx, lw, zlo[z] - 1u, &sw0), x1 = seg_x(lx, lw, last - 1u, &sw1);
        rect_to(ex, x0, ly, (x1 + sw1) - x0, ROW_H, z == 0 ? th_role[TR_OK] : z == 1 ? th_role[TR_WARN] : th_role[TR_DANGER]);
    }
    uint32_t peak_seg = ((uint32_t)pk[ch].peak * VUM_SEGMENTS) / 255u; if (peak_seg > VUM_SEGMENTS) peak_seg = VUM_SEGMENTS;
    if (peak_seg > lit && peak_seg > 0u) {
        uint32_t sw; const uint32_t px = seg_x(lx, lw, peak_seg - 1u, &sw);
        const uint32_t zone = peak_seg <= 16u ? 0u : peak_seg <= 21u ? 1u : 2u;
        rect_to(ex, px, ly + (ROW_H - PEAK_MARK_H) / 2u, sw, PEAK_MARK_H, zone == 0 ? th_role[TR_OK] : zone == 1 ? th_role[TR_WARN] : th_role[TR_DANGER]);
    }
}
int main(int argc, char **argv)
{
    rng = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u; int steps = argc > 2 ? atoi(argv[2]) : 1500;
    th_role[TR_OK] = 0x0600; th_role[TR_WARN] = 0xFE60; th_role[TR_DANGER] = 0xF9C0; th_role[TR_TEXT_SECONDARY] = 0x94B2; th_role[TR_FAINT] = 0x6B4D;
    uint8_t spec[MTR_BANDS] = { 0 }; int bad = 0, loud = 0;
    const uint32_t BX = 20, BY = 100, BW = 360, BH = 60, lx = BX + LABEL_W, lw = BW - LABEL_W;
    for (uint32_t i = 0; i < W * H; i++) fb[i] = 0;
    for (int n = 0; n < steps; n++) {
        if (n % 25 == 0) loud = (int)(rnd() % 4);
        const uint32_t base = loud == 3 ? 24000u : loud == 2 ? 9000u : loud == 1 ? 600u : 8u;
        mtr_in_t in = { spec, (uint32_t)n, 26, (uint16_t)BX, (uint16_t)BY, (uint16_t)BW, (uint16_t)BH, 0, (uint8_t)(n == 0 || n % 211 == 0), (uint8_t)(n % 307 > 295), rnd() % (base + 1u), rnd() % (base + 1u) };
        if (in.force) for (uint32_t i = 0; i < W * H; i++) fb[i] = (rnd() & 1) ? 0 : 0x1234;
        meter_vu_master_tick(&in);
        for (uint32_t i = 0; i < W * H; i++) ex[i] = 0;
        expect_ladder(0, lx, BY, lw, 0); expect_ladder(1, lx, BY + ROW2_Y, lw, 0);
        for (uint32_t y = BY; y < BY + 2u * ROW2_Y; y += 1) for (uint32_t x = lx; x < lx + lw; x++) {
            if (y >= BY + ROW_H && y < BY + ROW2_Y) continue;                  /* the scale line and ticks between the ladders are not part of this check */
            if (y >= BY + ROW2_Y + ROW_H) continue;
            if (fb[y * W + x] != ex[y * W + x]) { bad++; goto next; }
        }
    next:;
    }
    printf("MISMATCH %d TICKS %d\n", bad, steps);
    return 0;
}
