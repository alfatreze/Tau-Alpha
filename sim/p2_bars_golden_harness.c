/* Golden frames for fw2/ui/meter_winamp_bars.c: reads the scenarios written by sim/test_p2_meter_golden.py (the same ones the JS preview module runs) and prints every gfx_rect the module issues. */
#include <stdio.h>
#include <stdlib.h>
#include "../fw2/ui/meter_winamp_bars.c"
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { printf("rect %u %u %u %u %u\n", x, y, w, h, c); }
static void reset(void) { for (uint32_t i = 0; i < BANDS_MAX; i++) { disp[i] = drawn[i] = peak_drawn[i] = 0; vel[i] = 0; pk[i].peak = 0; pk[i].vel = 0; pk[i].hold = 0; } }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r"); int n; if (fscanf(f, "%d", &n) != 1) return 1;
    for (int s = 0; s < n; s++) {
        int np, nf; unsigned acc, prim, track, bg, bx, by, bw, bh;
        if (fscanf(f, "%d %u %u %u %u %u %u %u %u %d", &np, &acc, &prim, &track, &bg, &bx, &by, &bw, &bh, &nf) != 10) return 1;
        for (int i = 0; i < np; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 1; meter_winamp_bars_mv[i] = (uint16_t)v; }
        meter_winamp_bars_apply(); reset();
        th_role[TR_ACCENT] = (uint16_t)acc; th_role[TR_TEXT_PRIMARY] = (uint16_t)prim; th_role[TR_SURFACE_TRACK] = (uint16_t)track;
        for (int k = 0; k < nf; k++) {
            unsigned pz; uint8_t spec[MTR_BANDS]; int8_t wave[64];
            if (fscanf(f, "%u", &pz) != 1) return 1;
            for (int i = 0; i < 16; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 1; spec[i] = (uint8_t)v; }
            for (int i = 0; i < 64; i++) { int v; if (fscanf(f, "%d", &v) != 1) return 1; wave[i] = (int8_t)v; }
            printf("F %d\n", k);
            mtr_in_t in = {0}; in.spec = spec; in.wave = wave; in.force = (k == 0); in.paused = (uint8_t)pz; in.dt_ms = 26u;
            in.x = (uint16_t)bx; in.y = (uint16_t)by; in.w = (uint16_t)bw; in.h = (uint16_t)bh; in.bg = (uint16_t)bg;
            meter_winamp_bars_tick(&in);
        }
        printf("S\n");
    }
    return 0;
}
