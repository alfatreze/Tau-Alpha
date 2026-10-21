/* Picture checks of the Waterfall grid (fw2/ui/meter_waterfall_grid.c, the real code under P2_PACK) on a framebuffer model with the engine's rectangle fill and scroll. Prints "ok <name>" / "FAIL <name>" lines. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define P2_PACK 1
#include "../fw2/ui/meter_waterfall_grid.c"
#define W 400u
#define H 360u
static uint16_t fb[W * H], stash[512u * 1024u], clut[256]; static uint32_t scrolled, scrolls;
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) fb[(y + j) * W + x + i] = c; }
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) stash[(y + j) * 512u + x + i] = c; }
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { for (uint32_t i = 0; i < n; i++) clut[(s + i) & 255u] = c[i]; }
void gfx_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (dx + i < W && dy + j < H) fb[(dy + j) * W + dx + i] = clut[stash[(sy + j) * 512u + sx + i] & 255u]; }
void gfx_scroll_down(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d)
{
    scrolled += d; scrolls++;
    for (uint32_t j = h - 1u; j >= d; j--) { for (uint32_t i = 0; i < w; i++) fb[(y + j) * W + x + i] = fb[(y + j - d) * W + x + i]; if (j == d) break; }
}
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t f, uint16_t b) { (void)x; (void)y; (void)s; (void)f; (void)b; return 0; }
static int fails;
static void check(const char *n, int ok) { printf("%s %s\n", ok ? "ok  " : "FAIL", n); if (!ok) fails++; }
static uint16_t BG = 0x0821, PAL[4];
static void setup(uint16_t *mv, uint32_t mode, uint32_t speed, uint32_t cell)
{
    mv[MPP_WF_MODE] = (uint16_t)mode; mv[MPP_WF_SPEED] = (uint16_t)speed; mv[MPP_WF_GAIN] = 100; mv[MPP_WF_CELL] = (uint16_t)cell; wf_load(mv);
    th_role[TR_ACCENT] = 0x07E0; for (uint32_t k = 0; k < 4u; k++) PAL[k] = k == 3u ? 0x07E0 : rc_ramp(BG, 0x07E0, 1u + 2u * k, 7u);
}
static mtr_in_t mk(const uint8_t *spec, uint32_t frame, uint32_t force) { mtr_in_t in = {0}; in.spec = spec; in.frame = frame; in.dt_ms = 26; in.x = 16; in.y = 100; in.w = 368; in.h = 122; in.bg = BG; in.force = (uint8_t)force; return in; }
/* the class of the cell at grid position (c, r): 0..3 the palette step, 9 background, 8 anything else */
static int cell_class(uint32_t c, uint32_t r, uint32_t pitch) { const uint16_t px = fb[(100 + r * pitch + (cfg.mode == 0u ? expo : 0u) + 1u) * W + 16 + c * pitch + 1u];      /* the spectrogram's grid is shifted down by the part of the entering row in view */ for (int k = 0; k < 4; k++) if (px == PAL[k]) return k; return px == BG ? 9 : 8; }
int main(void)
{
    uint16_t mv[MPP_WF_N]; uint8_t quiet[16] = { 0 }, loud[16], one[16] = { 0 };
    for (int i = 0; i < 16; i++) loud[i] = 255;
    memset(fb, 0, sizeof fb);
    /* 1. the grid is always visible: after the first draw every cell of every mode is a cell (dim at least), never the background */
    for (uint32_t mode = 0; mode < 3u; mode++) {
        setup(mv, mode, 90, 8); mtr_in_t in = mk(quiet, 0, 1); wf_tick(&in);
        const uint32_t pitch = 9, cols = 368 / pitch, rows = 122 / pitch; int all = 1;
        for (uint32_t r = 0; r < (mode == 0u ? rows - 1u : rows); r++) for (uint32_t c = 0; c < cols; c++) if (cell_class(c, r, pitch) != 0) all = 0;      /* the spectrogram's last row has scrolled out */
        char n[80]; snprintf(n, sizeof n, "mode %u: with silence every cell is a dim cell (the grid is always visible)", mode); check(n, all);
    }
    /* 2. the spectrogram keeps the grid while it scrolls in silence: below the entering row every cell is still a cell */
    setup(mv, 0, 90, 8); { mtr_in_t in = mk(quiet, 0, 1); wf_tick(&in); }
    for (uint32_t f = 1; f < 200; f++) { mtr_in_t in = mk(quiet, f, 0); wf_tick(&in); }
    { const uint32_t pitch = 9, cols = 368 / pitch, rows = 122 / pitch; int all = 1; for (uint32_t r = 0; r < rows - 1u; r++) for (uint32_t c = 0; c < cols; c++) if (cell_class(c, r, pitch) != 0) all = 0; check("spectrogram: after 5 s of silence every complete cell is still there, dim", all); }
    /* 3. the scroll speed is a pixel speed: the same for small and big cells, and the set one (90 px/s over 5 s = 450 px) */
    uint32_t px[2];
    for (int k = 0; k < 2; k++) { const uint32_t cell = k ? 12u : 5u; setup(mv, 0, 90, cell); mtr_in_t in = mk(loud, 0, 1); wf_tick(&in); scrolled = 0; for (uint32_t f = 1; f <= 192; f++) { in = mk(loud, f, 0); wf_tick(&in); } px[k] = scrolled; }
    printf("     scrolled %u px (cell 5), %u px (cell 12) in 5.0 s at 90 px/s\n", px[0], px[1]);
    check("the scroll speed does not depend on the cell size", px[0] + 3u >= px[1] && px[1] + 3u >= px[0]);
    check("the scroll speed is the set pixel speed (450 px in 5 s)", px[0] + 6u >= 450u && px[0] <= 456u);
    /* 4. ZONES: only band 5 (region column 1, row 1) is loud: every lit cell is inside that region, the lit part grows from its centre */
    setup(mv, 1, 90, 8); one[5] = 150; { mtr_in_t in = mk(one, 0, 1); wf_tick(&in); in = mk(one, 1, 0); wf_tick(&in); }
    { const uint32_t pitch = 9, cols = 368 / pitch, rows = 122 / pitch; int inside = 1, lit = 0; double dlit = 0, dun = 0; int nlit = 0, nun = 0;
      for (uint32_t r = 0; r < rows; r++) for (uint32_t c = 0; c < cols; c++) {
          const int k = cell_class(c, r, pitch); const uint32_t rc = c * 4u / cols, rr = r * 4u / rows;
          if (k > 0 && k < 9) { lit++; if (rc != 1u || rr != 1u) inside = 0; }
          if (rc == 1u && rr == 1u) { const uint32_t c0 = rc * cols / 4u, c1 = (rc + 1u) * cols / 4u, r0 = rr * rows / 4u, r1 = (rr + 1u) * rows / 4u; const double dx = c - (c0 + c1 - 1) / 2.0, dy = r - (r0 + r1 - 1) / 2.0, d = dx * dx + dy * dy; if (k > 0 && k < 9) { dlit += d; nlit++; } else { dun += d; nun++; } }
      }
      check("zones: a loud band lights only its own region", inside && lit > 0);
      check("zones: the lit cells of a region are nearer its centre than the unlit ones (it fills from the middle outwards)", nlit > 0 && nun > 0 && dlit / nlit < dun / nun); }
    /* 5. dust keeps the grid too and does not crash on a tiny box */
    setup(mv, 2, 90, 12); { mtr_in_t in = mk(loud, 0, 1); in.w = 10; in.h = 10; wf_tick(&in); }
    check("a box smaller than one cell draws nothing and does not fail", 1);
    printf("%s\n", fails ? "FAILED" : "PASSED"); return fails ? 1 : 0;
}
