#!/usr/bin/env python3
"""Host test for fw/helios.inc's exclusion clipping (fig_rect_fn / fig_bar_fn, which the fullscreen figures call while fig_clip_on is set).
Draws random rects and OP_BAR-style bars on a pixel grid twice -- unclipped, and clipped around random exclusion rects -- and checks that (1) no
clipped draw touches an excluded pixel and (2) every pixel outside the exclusions equals the unclipped result. fb_bar models OP_BAR: the bottom
`lit` rows of the rect take fg, the rest bg."""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define GW 48
#define GH 36
static uint16_t grid[GH][GW];
static uint8_t fig_clip_on;
#define COLD_FN3
static uint32_t cmds;
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    if (!w || !h) return;
    for (uint32_t j = y; j < y + h && j < GH; j++) for (uint32_t i = x; i < x + w && i < GW; i++) grid[j][i] = c;
    cmds++;
}
static void fb_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg)
{
    if (!w || !h) return;
    if (lit > h) lit = h;
    for (uint32_t j = y; j < y + h && j < GH; j++) for (uint32_t i = x; i < x + w && i < GW; i++) grid[j][i] = (j >= y + h - lit) ? fg : bg;
    cmds++;
}
static uint32_t fake_scan;
#define R_SCAN 0x800000E8u
#define R_VBLANK 0x800000D0u
#define REG(a) (*(volatile uint32_t *)(&fake_scan))
static uint8_t cold_code_ok;
#include "%s"
static uint32_t rng = 12345u;
static uint32_t rnd(uint32_t n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) %% n; }
int main(void) {
    int bad = 0;
    for (int t = 0; t < 4000; t++) {
        helios_excl_n = 0; helios_exclude_clear(0); helios_exclude_clear(1);
        const int32_t ex = (int32_t)rnd(GW - 4), ey = (t & 1) ? 0 : (int32_t)rnd(GH - 4), ew = 1 + (int32_t)rnd(GW - ex), eh = 1 + (int32_t)rnd(GH - ey > 12 ? 12 : GH - ey);
        helios_exclude_set(0, ex, ey, ew, eh);
        const uint32_t x = rnd(GW - 2), y = rnd(GH - 2), w = 1 + rnd(GW - x), h = 1 + rnd(GH - y), lit = rnd(h + 3), isbar = rnd(2);
        const uint16_t fg = 7, bg = 3;
        for (int j = 0; j < GH; j++) for (int i = 0; i < GW; i++) grid[j][i] = 100;
        fig_clip_on = 0;
        if (isbar) fb_bar(x, y, w, h, lit, fg, bg); else fb_rect(x, y, w, h, fg);
        uint16_t ref[GH][GW]; for (int j = 0; j < GH; j++) for (int i = 0; i < GW; i++) ref[j][i] = grid[j][i];
        for (int j = 0; j < GH; j++) for (int i = 0; i < GW; i++) grid[j][i] = 100;
        fig_clip_on = 1;
        if (isbar) fig_bar_fn(x, y, w, h, lit, fg, bg); else fig_rect_fn(x, y, w, h, fg);
        if (!fig_clip_on) { printf("flag lost\n"); bad++; }
        fig_clip_on = 0;
        for (int j = 0; j < GH; j++) for (int i = 0; i < GW; i++) {
            const int inex = i >= ex && i < ex + ew && j >= ey && j < ey + eh;
            if (inex && grid[j][i] != 100) { if (bad++ < 5) printf("touched excluded pixel t=%%d bar=%%u (%%d,%%d)\n", t, isbar, i, j); }
            if (!inex && grid[j][i] != ref[j][i]) { if (bad++ < 5) printf("mismatch t=%%d bar=%%u (%%d,%%d) got %%u want %%u\n", t, isbar, i, j, grid[j][i], ref[j][i]); }
        }
    }
    printf(bad ? "FAILED %%d\n" : "PASSED\n", bad);
    return bad != 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    src = Path(td) / "t.c"
    src.write_text(HARNESS % str(ROOT / "fw/helios.inc"))
    exe = Path(td) / "t"
    r = subprocess.run(["cc", "-std=c11", "-Wall", "-Wno-unused-function", "-o", str(exe), str(src)], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    p = subprocess.run([str(exe)], capture_output=True, text=True)
    print(p.stdout.strip()); sys.exit(p.returncode)
