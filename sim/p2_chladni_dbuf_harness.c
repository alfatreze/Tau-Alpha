/* Chladni through the real graphics layer and the engine model, composed in the idle buffer and presented (as meters.c does for a whole-box meter): after every figure the SHOWN buffer's box equals the tile plane
 * (stash rows) scaled by the cell size and repeated across the box, the drawing target is back on the displayed buffer, and nothing outside the box changed. Runs once with the displayed buffer 0 and once after a flip (1).
 * argv: seed figures. Prints MISMATCH n FIGURES n OUTSIDE n TARGETBAD n. */
#include "p2_engine_model.h"
#include "../fw2/platform/platform.h"
caps_t caps;
#include "../fw2/ui/theme.h"
uint16_t th_role[TR_COUNT];
#include "../fw2/gfx/gfx.c"
#include "../fw2/ui/meter_chladni.c"
static uint32_t rng; static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(int argc, char **argv)
{
    rng = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u; const int steps = argc > 2 ? atoi(argv[2]) : 120;
    em_reset(); caps.dbuf = 1; th_role[TR_ACCENT] = 0xF7BE; th_role[TR_ERROR] = 0xF800;
    for (uint32_t i = 0; i < EM_VISIBLE; i++) { em_mem[i] = 0x7777; em_mem[EM_BUF_PX + i] = 0x7777; }
    const uint32_t BX = 20, BY = 100, BW = 360, BH = 110; uint8_t spec[MTR_BANDS];
    int bad = 0, figs = 0, outside = 0, tbad = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) { const int a = gfx_frame_begin(); gfx_frame_end(a); }            /* a flip: the displayed buffer is now 1 */
        for (int n = 0; n < steps; n++) {
            for (uint32_t b = 0; b < MTR_BANDS; b++) spec[b] = (uint8_t)((rnd() & 1u) ? rnd() : spec[b] / 2u);
            mtr_in_t in = { spec, (uint32_t)n, 26, BX, BY, BW, BH, 0x0000u, (uint8_t)(n == 0), 0, 0, 0, 0 };
            const uint32_t before = chladni_drawn(), front = em_disp;
            const int comp = gfx_compose_begin(BX, BY, BW, BH);
            meter_chladni_tick(&in);
            if (comp) gfx_compose_present(BX, BY, BW, BH);
            gfx_fence();
            if (gfx_cur_buf() != em_disp || em_cpu != em_disp) tbad++;
            for (uint32_t y = 0; y < 360u; y++) for (uint32_t x = 0; x < 400u; x++) if ((x < BX || x >= BX + BW || y < BY || y >= BY + BH) && em_px(front, x, y) != 0x7777) outside++;
            if (chladni_drawn() == before) continue;
            figs++;
            const uint32_t cell = pre->cell, rx = pre->Rx, ry = pre->Ry;
            const uint32_t sw = rx * cell > 127u ? 127u / cell : rx, sh = ry * cell > BH ? BH / cell : ry, tw = sw * cell, th = sh * cell;
            int diff = 0;
            for (uint32_t y = 0; y < BH && !diff; y++) for (uint32_t x = 0; x < BW; x++)
                if (em_px(front, BX + x, BY + y) != em_mem[(CHL_PLANE_Y + (y % th) / cell) * EM_STRIDE + (x % tw) / cell]) { diff = 1; break; }
            bad += diff;
        }
    }
    printf("MISMATCH %d FIGURES %d OUTSIDE %d TARGETBAD %d\n", bad, figs, outside, tbad);
    return 0;
}
