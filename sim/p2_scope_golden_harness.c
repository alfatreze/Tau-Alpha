/* Golden frames for fw2/ui/meter_winamp_scope.c: reads the scenarios written by sim/test_p2_meter_golden.py (the same ones the JS preview module runs: parameters, then per frame the paused flag and the 256-column
 * capture) and prints every gfx_rect the module issues. The hardware capture block is a model that serves the frame's capture; it is never busy; the blend is absent (the wipe path). */
#include <stdio.h>
#include <stdlib.h>
#include "../fw2/ui/meter_winamp_scope.c"
uint16_t th_role[TR_COUNT];
static int16_t cmin[256], cmax[256]; static uint32_t idx_sel;
uint32_t host_rd(uint32_t a)
{
    if (a == 0x800000FCu) return 1u;
    if (a == 0x800000F4u) return ((uint32_t)(uint16_t)cmin[idx_sel & 255u] << 16) | (uint16_t)cmax[idx_sel & 255u];
    return 0;
}
void host_wr(uint32_t a, uint32_t v) { if (a == 0x800000F0u) idx_sel = v; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { printf("rect %u %u %u %u %u\n", x, y, w, h, c); }
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
int gfx_blend_ready(void) { return 0; } void gfx_blend_on(uint32_t a) { (void)a; } void gfx_blend_off(void) { }
void gfx_blend_blit(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r"); int n; if (fscanf(f, "%d", &n) != 1) return 1;
    for (int s = 0; s < n; s++) {
        int np, nf; unsigned acc, prim, track, bg, bx, by, bw, bh;
        if (fscanf(f, "%d %u %u %u %u %u %u %u %u %d", &np, &acc, &prim, &track, &bg, &bx, &by, &bw, &bh, &nf) != 10) return 1;
        uint16_t params[8]; for (int i = 0; i < np; i++) { unsigned v; if (fscanf(f, "%u", &v) != 1) return 1; params[i] = (uint16_t)v; }
        ws_load(params); sc_pk = 1u; armed = 0; init = 0; strip_h = 0;
        for (int i = 0; i < 64; i++) sc_mid[i] = 0;
        th_role[TR_ACCENT] = (uint16_t)acc; th_role[TR_TEXT_PRIMARY] = (uint16_t)prim; th_role[TR_SURFACE_TRACK] = (uint16_t)track;
        for (int k = 0; k < nf; k++) {
            unsigned pz; if (fscanf(f, "%u", &pz) != 1) return 1;
            for (int i = 0; i < 256; i++) { int a, b; if (fscanf(f, "%d %d", &a, &b) != 2) return 1; cmin[i] = (int16_t)a; cmax[i] = (int16_t)b; }
            printf("F %d\n", k);
            static uint8_t spec[MTR_BANDS];
            mtr_in_t in = {0}; in.spec = spec; in.force = (k == 0); in.paused = (uint8_t)pz; in.dt_ms = 26u;
            in.x = (uint16_t)bx; in.y = (uint16_t)by; in.w = (uint16_t)bw; in.h = (uint16_t)bh; in.bg = (uint16_t)bg;
            meter_winamp_scope_tick(&in);
        }
        printf("S\n");
    }
    return 0;
}
