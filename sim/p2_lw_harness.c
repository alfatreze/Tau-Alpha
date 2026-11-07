/* Host check of fw2/ui/meter_layered_wave.c (the shims around the old player's golden-tested body): every rectangle stays inside the meter's box, every preset draws, the history ring fills only when the PSRAM window
 * is proven, and a missing window gives a message and no store. argv: seed steps psram(1 = the controller is in the bitstream). Prints OUTSIDE n RECTS n TEXT n RING n. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t psram_mem[0x800];
#define __psram_state_start psram_mem
#define __psram_state_end (psram_mem + 0x800)
#include "../fw2/ui/meter_layered_wave.c"
#include "../fw2/generated/factory_presets.h"
static int psram_ok; static uint32_t cyc;
uint32_t host_rd(uint32_t a)
{
    if (a == 0x80000088u) return psram_ok ? 0x50535231u : 0u;
    if (a == 0x800000A0u) return 1u << 16;
    cyc += 3000u; return cyc;
}
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT];
static uint32_t bx, by, bw, bh, outside, rects, texts;
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)c; rects++; if (x < bx || y < by || x + w > bx + bw || y + h > by + bh) outside++; }
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { (void)sx; (void)sy; rects++; if (dx < bx || dy < by || dx + w > bx + bw || dy + h > by + bh) outside++; }
void gfx_scroll_left(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d) { (void)d; rects++; if (x < bx || y < by || x + w > bx + bw || y + h > by + bh) outside++; }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { (void)y; (void)s; (void)fg; (void)bg; texts++; return x; }
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(int argc, char **argv)
{
    rng = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u; int steps = argc > 2 ? atoi(argv[2]) : 120; psram_ok = argc > 3 ? atoi(argv[3]) : 1;
    th_role[TR_ACCENT] = 0xF7BEu; th_role[TR_BASE] = 0x0841u; th_role[TR_BG_BOTTOM] = 0x1082u; th_role[TR_TEXT_PRIMARY] = 0xFFFFu; th_role[TR_ERROR] = 0xF800u;
    bx = 0; by = 120; bw = 400; bh = 110;
    uint8_t spec[MTR_BANDS]; uint32_t ring_nz = 0, drawing_presets = 0;
    for (uint32_t p = 0; p < FP_LW_N; p++) {
        for (uint32_t k = 0; k < MP_LAYERED_WAVE_N; k++) lw_mv[k] = fp_lw[p][k]; want = p; lw_init = 0; lw_head = 0; memset(lw_ring, 0, sizeof lw_ring); uint32_t r0 = rects;
        for (int n = 0; n < steps; n++) {
            for (uint32_t b = 0; b < MTR_BANDS; b++) spec[b] = (uint8_t)((rnd() & 1u) ? rnd() : spec[b] / 2u);
            mtr_in_t in = { spec, (uint32_t)n, 26, (uint16_t)bx, (uint16_t)by, (uint16_t)bw, (uint16_t)bh, 0x1082u, (uint8_t)(n == 0), 0, rnd() % 20000u, rnd() % 20000u, 0 };
            meter_layered_wave_tick(&in);
        }
        if (rects > r0) drawing_presets++;
        for (uint32_t k = 0; k < LW_MAXL; k++) for (uint32_t i = 0; i < LW_RW; i++) if (lw_ring[k][i]) ring_nz++;
    }
    printf("OUTSIDE %u RECTS %u TEXT %u RING %u PRESETS %u\n", outside, rects, texts, ring_nz, drawing_presets);
    return 0;
}
