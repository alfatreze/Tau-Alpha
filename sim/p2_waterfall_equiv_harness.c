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
void gfx_scroll_left(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d) { cmds += 4; for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i + d < w; i++) fb[(y + j) * W + x + i] = fb[(y + j) * W + x + i + d]; }
void gfx_clut_load(uint32_t s, const uint16_t *c, uint32_t n) { for (uint32_t i = 0; i < n; i++) clut[(s + i) & 255u] = c[i]; }
void gfx_cblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h)
{
    cmds++; if (w > 127u) { printf("FAIL cblit wider than 127\n"); exit(1); }
    for (uint32_t j = 0; j < h; j++) for (uint32_t i = 0; i < w; i++) { if (dx + i >= W || dy + j >= H) { printf("FAIL cblit outside the frame\n"); exit(1); } fb[(dy + j) * W + dx + i] = clut[stash[(sy + j) * 512u + sx + i] & 255u]; }
}
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t f, uint16_t b) { (void)x; (void)y; (void)s; (void)f; (void)b; return 0; }
static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
#ifdef FLOW_TEST
/* The horizontal spectrogram (sim/test_p2_waterfall_flow.py). "B band meanrow nbright": only that band lit for 120 ticks, the mean row (0 top .. 1000 bottom) of the bright pixels; "P tick meanx n": a one-tick full-spectrum pulse
 * after silence, the mean x of the bright pixels on every following tick (it must move left at the scroll speed); "O outside": pixels changed outside the box; "R rightedge": bright pixels in the last 8 columns after the pulse passed. */
static uint32_t bright(uint16_t c) { return ((c >> 11) & 31u) * 8u * 30u / 100u + ((c >> 5) & 63u) * 4u * 59u / 100u + (c & 31u) * 8u * 11u / 100u; }
int main(void)
{
    th_role[TR_ACCENT] = 0x07E0;
    uint16_t mv[MPP_WF_N]; uint8_t spec[16];
    const uint32_t BX = 16, BY = 20, BW = 368, BH = 240;
    for (uint32_t band = 0; band < 16; band += 15) {
        mv[MPP_WF_MODE] = 0; mv[MPP_WF_SPEED] = 120; mv[MPP_WF_GAIN] = 100; mv[MPP_WF_CELL] = 7; mv[MPP_WF_FADE] = 450; mv[MPP_WF_PALETTE] = 1; mv[MPP_WF_FLOW] = 1; wf_load(mv);
        memset(fb, 0, sizeof fb); memset(stash, 0xA5, sizeof stash);
        for (uint32_t n = 0; n < 120; n++) {
            for (uint32_t b = 0; b < 16; b++) spec[b] = (uint8_t)(b == band ? 255 : 0);
            mtr_in_t in = {0}; in.spec = spec; in.frame = n; in.dt_ms = 40; in.x = BX; in.y = BY; in.w = BW; in.h = BH; in.bg = 0x0821; in.force = (uint8_t)(n == 0); wf_tick(&in);
        }
        unsigned long sy = 0, nb = 0;
        for (uint32_t y = BY; y < BY + BH; y++) for (uint32_t x = BX; x < BX + BW; x++) if (bright(fb[y * W + x]) > 120) { sy += y - BY; nb++; }
        printf("B %u %lu %lu\n", band, nb ? sy * 1000ul / nb / BH : 0ul, nb);
    }
    mv[MPP_WF_SPEED] = 120; wf_load(mv);
    memset(fb, 0, sizeof fb); memset(stash, 0xA5, sizeof stash);
    unsigned long outside = 0;
    for (uint32_t n = 0; n < 70; n++) {
        for (uint32_t b = 0; b < 16; b++) spec[b] = (uint8_t)((n == 20 || n == 21) ? 255 : 0);
        mtr_in_t in = {0}; in.spec = spec; in.frame = n; in.dt_ms = 40; in.x = BX; in.y = BY; in.w = BW; in.h = BH; in.bg = 0x0821; in.force = (uint8_t)(n == 0); wf_tick(&in);
        unsigned long sx = 0, nb = 0;
        for (uint32_t y = BY; y < BY + BH; y++) for (uint32_t x = BX; x < BX + BW; x++) if (bright(fb[y * W + x]) > 120) { sx += x - BX; nb++; }
        if (n >= 20) printf("P %u %lu %lu\n", n, nb ? sx * 10ul / nb : 0ul, nb);
        if (n == 69) { unsigned long r = 0; for (uint32_t y = BY; y < BY + BH; y++) for (uint32_t x = BX + BW - 8; x < BX + BW; x++) if (bright(fb[y * W + x]) > 120) r++; printf("R %lu\n", r); }
    }
    for (uint32_t y = 0; y < H; y++) for (uint32_t x = 0; x < W; x++) if (!(y >= BY && y < BY + BH && x >= BX && x < BX + BW) && fb[y * W + x]) outside++;
    printf("O %lu\n", outside);
    return 0;
}
#elif defined(PALETTE_TEST)

/* The wide colour maps end to end (sim/test_p2_waterfall_palette.py): "C map v rgb565" for every level of every map, then for each map and several constant levels the spectrogram runs 160 ticks and
 * "S map level distinct luma" prints how many different colours the grid shows and their mean luma. */
int main(void)
{
    th_role[TR_ACCENT] = 0x07E0;
    for (uint32_t map = 1; map <= 3; map++) for (uint32_t v = 0; v < 256; v++) printf("C %u %u %u\n", map, v, wf_col(map, v));
    for (uint32_t map = 0; map <= 3; map++) for (uint32_t lv = 0; lv <= 306; lv += 51) {                     /* 306: a gradient across the bands */
        uint16_t mv[MPP_WF_N]; uint8_t spec[16];
        mv[MPP_WF_MODE] = 0; mv[MPP_WF_SPEED] = 200; mv[MPP_WF_GAIN] = 100; mv[MPP_WF_CELL] = 8; mv[MPP_WF_FADE] = 450; mv[MPP_WF_PALETTE] = (uint16_t)map; mv[MPP_WF_FLOW] = 0; wf_load(mv);
        memset(fb, 0, sizeof fb); memset(stash, 0xA5, sizeof stash);
        for (uint32_t b = 0; b < 16u; b++) spec[b] = (uint8_t)(lv > 255u ? b * 17u : lv);
        for (uint32_t n = 0; n < 160u; n++) { mtr_in_t in = {0}; in.spec = spec; in.frame = n; in.dt_ms = 40; in.x = 16; in.y = 20; in.w = 368; in.h = 270; in.bg = 0x0821; in.force = (uint8_t)(n == 0); wf_tick(&in); }
        static uint16_t seen[4096]; uint32_t ns = 0; unsigned long luma = 0, cnt = 0;
        for (uint32_t y = 20; y < 290; y++) for (uint32_t x = 16; x < 384; x++) { const uint16_t c = fb[y * W + x]; if (c == 0x0821) continue; int f = 0; for (uint32_t k = 0; k < ns; k++) if (seen[k] == c) { f = 1; break; } if (!f && ns < 4096) seen[ns++] = c;
            luma += (((c >> 11) & 31u) * 8u * 30u + ((c >> 5) & 63u) * 4u * 59u + (c & 31u) * 8u * 11u) / 100u; cnt++; }
        printf("S %u %u %u %lu\n", map, lv, ns, cnt ? luma / cnt : 0ul);
    }
    return 0;
}
#else
int main(void)
{
    uint16_t mv[MPP_WF_N]; uint8_t spec[16];
    th_role[TR_ACCENT] = 0x07E0; rng = 12345u;
    for (uint32_t scen = 0; scen < 6u; scen++) {
        const uint32_t cell = 5u + (scen * 3u) % 8u, speed = 20u + scen * 60u, pitch = cell + 1u;
        /* whole cells (the new code centres the grid, the reference does not) and within the reference's 48 x 40 cells */
        uint32_t boxw = (scen & 1u ? 368u : 400u) / pitch, boxh = (scen % 3u == 0u ? 122u : 323u) / pitch; if (boxw > 48u) boxw = 48u; if (boxh > 40u) boxh = 40u; boxw *= pitch; boxh *= pitch;
        mv[MPP_WF_MODE] = 0; mv[MPP_WF_SPEED] = (uint16_t)speed; mv[MPP_WF_GAIN] = 100; mv[MPP_WF_CELL] = (uint16_t)cell; mv[MPP_WF_PALETTE] = 0; mv[MPP_WF_FLOW] = 0; wf_load(mv);        /* THEME: the original three steps (the frozen reference has no wide maps) */
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
#endif
