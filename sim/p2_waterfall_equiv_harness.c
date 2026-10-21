/* The Waterfall's spectrogram, drawn with rectangles (the reference, sim/p2_waterfall_ref.c.txt: the code before B-802) and with the palette blit of its entering-row plane (fw2/ui/meter_waterfall_grid.c), on the same engine model
 * (rectangle fill, stash rectangle, scroll, palette load and palette blit): after EVERY tick the box must hold the same picture. Built twice by sim/test_p2_waterfall_blit.py with -DWF_SRC="<file>".
 * Prints one line per tick: a hash of the box, then "CMDS <total>". */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define P2_PACK 1
static unsigned long cmds;
#include WF_SRC
#define W 400u
#define H 360u
static uint16_t fb[W * H], stash[512u * 1024u], clut[256];
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { cmds++; for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) if (x + i < W && y + j < H) fb[(y + j) * W + x + i] = c; }
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { cmds++; for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) stash[(y + j) * 512u + x + i] = c; }
void gfx_scroll_down(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d) { cmds += 4; for (uint32_t j = h - 1u; j >= d; j--) { for (uint32_t i = 0; i < w; i++) fb[(y + j) * W + x + i] = fb[(y + j - d) * W + x + i]; if (j == d) break; } }
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { for (uint32_t i = 0; i < n; i++) clut[(s + i) & 255u] = c[i]; }
void gfx_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h)
{
    cmds++; if (w > 127u) { printf("FAIL cblit wider than 127\n"); exit(1); }
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) { if (dx + i >= W || dy + j >= H) { printf("FAIL cblit outside the frame\n"); exit(1); } fb[(dy + j) * W + dx + i] = clut[stash[(sy + j) * 512u + sx + i] & 255u]; }
}
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t f, uint16_t b) { (void)x; (void)y; (void)s; (void)f; (void)b; return 0; }
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
int main(void)
{
    uint16_t mv[MPP_WF_N]; uint8_t spec[16];
    th_role[TR_ACCENT] = 0x07E0; rng = 12345u;
    for (uint32_t scen = 0; scen < 6u; scen++) {
        const uint32_t cell = 5u + (scen * 3u) % 8u, speed = 20u + scen * 60u, boxw = scen & 1u ? 368u : 400u, boxh = scen % 3u == 0u ? 122u : 323u;
        mv[MPP_WF_MODE] = 0; mv[MPP_WF_SPEED] = (uint16_t)speed; mv[MPP_WF_GAIN] = 100; mv[MPP_WF_CELL] = (uint16_t)cell; wf_load(mv);
        memset(fb, 0, sizeof fb); memset(stash, 0xA5, sizeof stash);
        for (uint32_t n = 0; n < 400u; n++) {
            for (uint32_t b = 0; b < 16u; b++) spec[b] = (uint8_t)((n / 40u) % 3u == 0u ? 0u : rnd() % 256u);
            mtr_in_t in = {0}; in.spec = spec; in.frame = n; in.dt_ms = (uint16_t)(10u + rnd() % 60u); in.x = (uint16_t)((400u - boxw) / 2u); in.y = 20; in.w = (uint16_t)boxw; in.h = (uint16_t)boxh; in.bg = 0x0821;
            in.force = (uint8_t)(n == 0u || n == 150u); in.paused = (uint8_t)(n % 97u > 90u);
            if (n == 200u) memset(fb, 0x5A, sizeof fb);                                /* stale pixels: only a forced repaint may clear them, so force it */
            if (n == 200u) in.force = 1;
            wf_tick(&in);
            unsigned long hh = 1469598103934665603ul;
            for (uint32_t y = in.y; y < in.y + boxh; y++) for (uint32_t x = in.x; x < in.x + boxw; x++) { hh ^= fb[y * W + x]; hh *= 1099511628211ul; }
            printf("%lu\n", hh);
        }
    }
    printf("CMDS %lu\n", cmds);
    return 0;
}
