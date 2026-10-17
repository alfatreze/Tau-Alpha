#pragma GCC optimize ("Os")
#include "icon.h"
#include "gfx.h"
#include "../cores/icon_core.h"

static uint8_t built, bad;
static int ready(void)
{
    if (bad) return 0;
    if (!built) {
        uint32_t n = 0;
        if (ti_check(icon_rec, icon_rec_len, &n)) { bad = 1; return 0; }
        for (uint32_t id = 0; id < n; id++) {
            ti_icon_t ic;
            if (ti_find(icon_rec, icon_rec_len, id, &ic)) continue;
            for (uint32_t y = 0; y < ic.h; y++)
                for (uint32_t x = 0; x < ic.w; ) { uint32_t cov; const uint32_t r = ti_run(&ic, x, y, &cov); gfx_rect_stash(ic.plane_x + x, ICON_PLANE_Y + y, r, 1, (uint16_t)cov); x += r; }
        }
        built = 1;
    }
    return 1;
}
uint32_t icon_width(uint32_t id) { ti_icon_t ic; return (ready() && !ti_find(icon_rec, icon_rec_len, id, &ic)) ? ic.w : 0u; }
static int draw(uint32_t id, uint32_t x, uint32_t y, uint16_t fg, uint16_t bg, int stash)
{
    ti_icon_t ic;
    if (!ready() || ti_find(icon_rec, icon_rec_len, id, &ic)) return 1;
    gfx_clut_begin(0);
    for (uint32_t c = 0; c < 16u; c++) gfx_clut_put(ti_colour(bg, fg, c));
    if (stash) gfx_cblit_abs(ic.plane_x, ICON_PLANE_Y, x, y, ic.w, ic.h); else gfx_cblit(ic.plane_x, ICON_PLANE_Y, x, y, ic.w, ic.h);
    return 0;
}
int icon_draw(uint32_t id, uint32_t x, uint32_t y, uint16_t fg, uint16_t bg) { return draw(id, x, y, fg, bg, 0); }
int icon_draw_stash(uint32_t id, uint32_t x, uint32_t y, uint16_t fg, uint16_t bg) { return draw(id, x, y, fg, bg, 1); }
