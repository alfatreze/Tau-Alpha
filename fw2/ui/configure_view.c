#pragma GCC optimize ("Os")
#include "configure_view.h"
#include "meter.h"
#include "theme.h"
#include "helios.h"
#include "regions.h"
#include "views.h"
#include "../gfx/gfx.h"
#include "../transport/mp3play.h"
#include "../platform/platform.h"
#include "../cores/view_core.h"
#include "../cores/configure_core.h"

#define BG   UI_BG_SCREEN
#define PREV_X 20u
#define PREV_Y 26u
#define PREV_W 360u
#define PREV_H 100u
#define LIST_Y 134u
#define ROW_H  24u
_Static_assert(CF_VIS * ROW_H + LIST_Y <= 330u, "the list must leave the hint lines free");

static cf_t cf; static uint32_t meter, dirty;

static void put_num(char *o, uint32_t v)
{
    char t[8]; uint32_t k = 0, n = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) o[n++] = t[--k]; o[n] = 0;
}
static const meter_def_t *def(void) { return &meter_table[meter]; }
static void rebuild(void) { cf_build(&cf, (const cf_pd_t *)def()->pd, def()->np, def()->mv); }
static void paint_all(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    char t[32] = "CONFIGURE: "; uint32_t n = 11; for (const char *s = def()->name; *s && n < 30u; s++) t[n++] = *s; t[n] = 0;
    gfx_text(20, 4, t, UI_ACCENT, BG);
    meters_enter(PREV_X, PREV_Y, PREV_W, PREV_H);
    dirty = 1;
}
void configure_view_enter(void)
{
    meter = meters_cur(); cf.n = 0; cf.sel = 0; cf.top = 0;
    rebuild(); paint_all();
}
void configure_view_redraw(void) { paint_all(); }
void configure_view_mark(void) { helios_dirty(R_CONF_PREV); if (dirty) helios_dirty(R_CONF_LIST); }
void configure_view_draw_preview(void) { meters_tick(mp3play_np()->gov < 1u); }

void configure_view_draw_list(void)
{
    if (!dirty) return;
    dirty = 0;
    const meter_def_t *d = def();
    for (uint32_t k = 0; k < CF_VIS; k++) {
        const uint32_t i = cf.top + k, y = LIST_Y + k * ROW_H;
        if (i >= cf.n) { gfx_rect(0, y, GFX_W, ROW_H, BG); continue; }
        const int on = i == cf.sel; const uint16_t bg = on ? UI_ACCENT : BG, fg = on ? UI_ON_ACCENT : UI_WHITE, dm = on ? UI_ON_ACCENT : UI_DIM;
        const uint8_t id = cf.rows[i]; const char *label; char v[20] = "";
        if (id == CF_PRESET) { label = "PRESET"; const char *s = meters_preset_name(); uint32_t n = 0; while (*s && n < 18u) v[n++] = *s++; v[n] = 0; }
        else if (id == CF_RESET) label = "RESET TO PRESET";
        else {
            const mpd_t *p = &d->pd[id]; label = p->label; const uint32_t val = d->mv[id];
            if (p->type == 2u) { v[0] = val ? 'O' : 'O'; v[1] = val ? 'N' : 'F'; v[2] = val ? 0 : 'F'; v[3] = 0; }
            else if (p->type == 1u) { const char *s = p->vals[val < p->nval ? val : 0u]; uint32_t n = 0; while (*s && n < 18u) v[n++] = *s++; v[n] = 0; }
            else put_num(v, val);
        }
        gfx_rect(0, y, GFX_W, ROW_H, bg);
        gfx_text(16, y + 4, label, fg, bg);
        if (v[0]) gfx_text(GFX_W - 16 - gfx_text_width(v), y + 4, v, dm, bg);
    }
    gfx_rect(0, 330, GFX_W, 30, BG);
    gfx_text(16, 330, "UP DOWN  LEFT RIGHT CHANGE", UI_DIM, BG);
    gfx_text(16, 346, "A RESET  B BACK", UI_DIM, BG);
}

int configure_view_input(uint32_t edge)
{
    uint32_t k = 0;
    if (edge & KEY_UP) k |= CF_UP;
    if (edge & KEY_DOWN) k |= CF_DOWN;
    if (edge & KEY_LEFT) k |= CF_LEFT;
    if (edge & KEY_RIGHT) k |= CF_RIGHT;
    if (edge & KEY_A) k |= CF_A;
    if (edge & (KEY_B | KEY_START)) k |= CF_B;
    if (!k) return VW_STAY;
    const meter_def_t *d = def();
    const cf_effect_t e = cf_input(&cf, (const cf_pd_t *)d->pd, d->np, d->mv, k);
    dirty = 1;
    switch (e.kind) {
    case CFE_PARAM: meters_params_changed(meter); break;
    case CFE_PRESET: { const uint32_t n = meters_npre(meter), p = meters_preset_of(meter); meters_preset_select(meter, e.dir > 0 ? (p + 1u) % n : (p + n - 1u) % n); rebuild(); break; }
    case CFE_RESET: meters_preset_select(meter, meters_preset_of(meter)); rebuild(); break;
    case CFE_CLOSE: return VW_BACK;
    default: break;
    }
    return VW_STAY;
}
