/* Host check of the Winamp Scope trail (fw2/ui/meter_winamp_scope.c with the real graphics layer and the engine model): a trace drawn once and then not redrawn fades frame by frame toward the background by exactly the
 * blend the weight (100 - trail) percent gives; trail 0, and a bitstream without the blend, wipe the box instead; the box's surroundings are never touched. argv: trail blend(1/0) frames. Prints "k pixel" lines and "OUTSIDE n". */
#include "p2_engine_model.h"
#include "../fw2/platform/platform.h"
caps_t caps;
#include "../fw2/ui/theme.h"
uint16_t th_role[TR_COUNT];
#include "../fw2/gfx/gfx.c"
#include "../fw2/ui/meter_winamp_scope.c"
static int16_t cmin[256], cmax[256]; static uint32_t idx_sel;
static uint32_t ext_rd(uint32_t a) { if (a == 0x800000FCu) return 1u; if (a == 0x800000F4u) return ((uint32_t)(uint16_t)cmin[idx_sel & 255u] << 16) | (uint16_t)cmax[idx_sel & 255u]; return 0u; }
static void ext_wr(uint32_t a, uint32_t v) { if (a == 0x800000F0u) idx_sel = v; }
static uint16_t ch(uint16_t s, uint16_t d, uint32_t a, int sh, int bits)
{
    const uint32_t m = (1u << bits) - 1u; return (uint16_t)(((((s >> sh) & m) * a + ((d >> sh) & m) * (256u - a)) >> 8) << sh);
}
int main(int argc, char **argv)
{
    const uint32_t trail_pct = argc > 1 ? (uint32_t)atoi(argv[1]) : 30u; em_has_blend = argc > 2 ? (uint32_t)atoi(argv[2]) : 1u; const int frames = argc > 3 ? atoi(argv[3]) : 4;
    em_reset(); caps.dbuf = 1; em_ext_rd = ext_rd; em_ext_wr = ext_wr;
    for (uint32_t i = 0; i < EM_BUF_PX; i++) em_mem[i] = 0x7777;
    meter_winamp_scope_mv[MPP_SCOPE_SCOPE_TRAIL] = (uint16_t)trail_pct; meter_winamp_scope_mv[MPP_SCOPE_SCOPE_SMOOTH] = 0; meter_winamp_scope_apply();
    th_role[TR_SURFACE_TRACK] = 0x18E3; th_role[TR_ACCENT] = 0xFD05;
    const uint32_t BX = 20, BY = 100, BW = 360, BH = 120; const uint16_t BG = 0x0000, ACC = 0xFD05;
    uint8_t spec[MTR_BANDS] = { 0 };
    mtr_in_t in = { spec, 0, 26, BX, BY, BW, BH, BG, 1, 0, 0, 0, 0 };
    meter_winamp_scope_tick(&in); gfx_fence();                          /* the forced first call clears and arms */
    in.force = 0;
    for (int c = 0; c < 256; c++) { cmin[c] = -20000; cmax[c] = 20000; }  /* loud: the first frame is normalised against a peak of 1, so every bar spans the box */
    meter_winamp_scope_tick(&in); gfx_fence();
    const uint32_t px = BX + 2u, py = BY + 12u;                          /* inside the first bar, off the centre line */
    printf("0 %04x\n", em_px(0, px, py));
    for (int c = 0; c < 256; c++) { cmin[c] = 0; cmax[c] = 0; }          /* silence: the bars collapse to the centre line; the old trace is only faded */
    uint16_t v = ACC; const uint32_t a = (trail_pct && em_has_blend) ? (100u - trail_pct) * 256u / 100u : 0u;
    for (int k = 1; k <= frames; k++) {
        meter_winamp_scope_tick(&in); gfx_fence();
        v = a ? (uint16_t)(ch(BG, v, a, 11, 5) | ch(BG, v, a, 5, 6) | ch(BG, v, a, 0, 5)) : BG;           /* the independent expectation: dst = (src * a + dst * (256 - a)) >> 8 per channel, src = background */
        printf("%d %04x %04x\n", k, em_px(0, px, py), v);
    }
    uint32_t outside = 0;
    for (uint32_t y = 0; y < 360u; y++) for (uint32_t x = 0; x < 400u; x++) if ((x < BX || x >= BX + BW || y < BY || y >= BY + BH) && em_px(0, x, y) != 0x7777) outside++;
    printf("OUTSIDE %u\n", outside);
    return 0;
}
