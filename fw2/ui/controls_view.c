#pragma GCC optimize ("Os")
#include "controls_view.h"
#include "../gfx/gfx.h"
#include "../gfx/font.h"
#include "../gfx/icon.h"
#include "../transport/mp3play.h"
#include "../cores/controls_core.h"
#include "../cores/view_core.h"
#include "../platform/mmio.h"
#include "helios.h"
#include "regions.h"
#include "theme.h"

#define BG   UI_BG_SCREEN
#define FG   UI_WHITE
#define DIM  UI_DIM
#define ACC  UI_ACCENT
#define ROW_H 34u
#define TOP_Y 34u
#define KEY_X0 20u
#define WHAT_X 156u
#define ICON_R 380u                                   /* icons are right-aligned to here */
#define ICON_GAP 6u
static ctl_t pg; static uint8_t dirty; static uint8_t shown_vol = 0xFFu;

void controls_view_enter(void) { ctl_open(&pg); dirty = 1; gfx_rect(0, 0, GFX_W, GFX_H, BG); }
void controls_view_redraw(void) { gfx_rect(0, 0, GFX_W, GFX_H, BG); dirty = 1; }
int controls_view_input(uint32_t edge)
{
    const int back = ctl_input(&pg, edge);
    dirty = 1;
    return back ? VW_BACK : VW_STAY;
}
void controls_view_mark(void)
{
    const uint32_t v = mp3play_pref_get(PREF_VOLUME);
    if ((uint8_t)v != shown_vol) dirty = 1;                       /* the volume speaker follows the volume */
    if (dirty) helios_dirty(R_CONTROLS);
}
void controls_view_draw(void)
{
    if (!dirty) return;
    dirty = 0;
    const uint32_t vol = mp3play_pref_get(PREF_VOLUME);
    shown_vol = (uint8_t)vol;
    gfx_rect(0, 0, GFX_W, TOP_Y, BG);
    gfx_text(20, 6, "CONTROLS", ACC, BG);
    for (uint32_t k = 0; k < CTL_VIS; k++) {
        const uint32_t i = pg.top + k, y = TOP_Y + k * ROW_H;
        if (i >= CTL_N) { gfx_rect(0, y, GFX_W, ROW_H, BG); continue; }
        const int on = i == pg.sel;
        const uint16_t bg = on ? ACC : BG;
        const ctl_row_t *r = &ctl_rows[i];
        gfx_rect(0, y, GFX_W, ROW_H, bg);
        gfx_text(KEY_X0, y + 9, r->key, on ? UI_ON_ACCENT : FG, bg);
        gfx_text(WHAT_X, y + 9, r->what, on ? UI_ON_ACCENT : DIM, bg);
        uint32_t ids[3], n = 0, w = 0;
        for (uint32_t s = 0; s < 3u; s++) {
            uint32_t id = r->ic[s];
            if (id == CI_NONE) continue;
            if (id == CI_VOLUME) id = ctl_vol_icon(vol);
            ids[n++] = id; w += icon_width(id) + (n > 1u ? ICON_GAP : 0u);
        }
        uint32_t x = ICON_R - w;
        for (uint32_t s = 0; s < n; s++) { icon_draw(ids[s], x, y + 3u, on ? UI_ON_ACCENT : FG, bg); x += icon_width(ids[s]) + ICON_GAP; }
    }
    gfx_rect(0, 322, GFX_W, 38, BG);
    gfx_text(20, 322, "UP DOWN  SCROLL", DIM, BG);
    gfx_text(20, 340, "B BACK  START CLOSE", DIM, BG);
}
