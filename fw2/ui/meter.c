#pragma GCC optimize ("Os")
#include "meter.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../cores/spectrum_core.h"

#define R_SPEC_IDX  0x800000DCu
#define R_SPEC_DATA 0x800000E0u
#define BG   RGB565(0, 0, 0)
#define BAR  RGB565(255, 160, 40)
#define HOT  RGB565(255, 90, 40)
#define CAP  RGB565(255, 255, 255)
#define BAR_W 20u
#define GAP   2u
#define X0    24u
static uint32_t box_y, box_h, win_seen = 0xFFFFFFFFu;
static uint8_t  lvl[SPEC_BANDS], px[SPEC_BANDS], cap_px[SPEC_BANDS], cap_hold[SPEC_BANDS], drawn_px[SPEC_BANDS], drawn_cap[SPEC_BANDS];

void meter_enter(uint32_t y, uint32_t h)
{
    box_y = y; box_h = h;
    win_seen = caps.spec ? (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu : 0xFFFFFFFFu;      /* the window in progress is skipped: its mean mixes in what played before the meter was shown (a full bar that then collapsed) */
    gfx_rect(0, y, GFX_W, h, BG);
    for (uint32_t b = 0; b < SPEC_BANDS; b++) { lvl[b] = px[b] = cap_px[b] = cap_hold[b] = drawn_px[b] = drawn_cap[b] = 0; }
}

static int fetch(uint32_t *m)                    /* a new window, read coherently (the counter must not move while the 16 means are read) */
{
    uint32_t w0 = (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu;
    if (w0 == win_seen) return 0;
    for (uint32_t b = 0; b < SPEC_BANDS; b++) { REG_WR(R_SPEC_IDX, b); m[b] = REG_RD(R_SPEC_DATA) & 0xFFFFFu; }
    if (((REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu) != w0) return 0;
    win_seen = w0;
    return 1;
}

void meter_draw(int allowed)
{
    if (!caps.spec || !allowed) return;
    uint32_t m[SPEC_BANDS];
    if (!fetch(m)) return;
    for (uint32_t b = 0; b < SPEC_BANDS; b++) {
        lvl[b] = (uint8_t)spec_ballistics(lvl[b], spec_level(m[b], b));
        uint32_t h = (uint32_t)lvl[b] * (box_h - 4u) / 255u;
        px[b] = (uint8_t)h;
        if (h >= cap_px[b]) { cap_px[b] = (uint8_t)h; cap_hold[b] = 10; }          /* the cap rides the bar, holds a moment, then falls */
        else if (cap_hold[b]) cap_hold[b]--;
        else cap_px[b] = (uint8_t)(cap_px[b] > 2u ? cap_px[b] - 2u : 0u);
    }
    for (uint32_t b = 0; b < SPEC_BANDS; b++) {
        if (px[b] == drawn_px[b] && cap_px[b] == drawn_cap[b]) continue;
        uint32_t x = X0 + b * (BAR_W + GAP), base = box_y + box_h - 2u;            /* bars grow up from the base line */
        uint32_t old_top = drawn_px[b] > drawn_cap[b] ? drawn_px[b] : drawn_cap[b], new_top = px[b] > cap_px[b] ? px[b] : cap_px[b];
        if (old_top > new_top) gfx_rect(x, base - old_top - 2u, BAR_W, old_top - new_top + 3u, BG);     /* erase what is no longer covered (the cap is 2 px thick) */
        if (px[b]) gfx_rect(x, base - px[b], BAR_W, px[b], px[b] > (box_h * 3u) / 4u ? HOT : BAR);
        if (cap_px[b] > px[b] + 1u) gfx_rect(x, base - cap_px[b] - 2u, BAR_W, 2u, CAP);
        drawn_px[b] = px[b]; drawn_cap[b] = cap_px[b];
    }
}
