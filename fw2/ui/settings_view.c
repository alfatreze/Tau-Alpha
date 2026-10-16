#pragma GCC optimize ("Os")
#include "settings_view.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../transport/mp3play.h"
#include "../cores/menupages_core.h"
#include "../cores/view_core.h"
#include "nowplaying.h"
#include "views.h"
#include "regions.h"
#include "helios.h"

#define BG   RGB565(0, 0, 0)
#define FG   RGB565(255, 255, 255)
#define DIM  RGB565(140, 146, 160)
#define OFFC RGB565(90, 94, 104)
#define ACC  RGB565(255, 160, 40)
#define ROW_H 34u
#define TOP_Y 34u
/* The menu's value ids 0..6 are the transport's preferences. */
_Static_assert(V_VOLUME == PREF_VOLUME && V_RG == PREF_RG && V_TEMPO == PREF_TEMPO && V_REPEAT == PREF_REPEAT && V_SHUFFLE == PREF_SHUFFLE && V_SEEKSTEP == PREF_SEEKSTEP && V_SKIPSIL == PREF_SKIPSIL, "menu value ids must mirror PREF_*");

static mg_t menu; static uint32_t vals[V_COUNT], dirty;

static void pull(void)
{
    for (uint32_t i = 0; i < PREF_COUNT; i++) { uint32_t v = mp3play_pref_get(i); if (v != vals[i]) { vals[i] = v; dirty = 1; } }
    if (vals[V_ART] != np_art_on) { vals[V_ART] = np_art_on; dirty = 1; }
    if (vals[V_ACCEPT_ALL] != mp3play_accept_all) { vals[V_ACCEPT_ALL] = mp3play_accept_all; dirty = 1; }
}
static void put_num(char *o, uint32_t v)
{
    char t[8]; uint32_t k = 0, n = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) o[n++] = t[--k]; o[n] = 0;
}
static void value_text(const mg_row_t *r, char *o)
{
    const char *s = 0; uint32_t v = vals[r->arg], n = 0;
    switch (r->kind) {
    case RK_GROUP: s = ">"; break;
    case RK_TOGGLE: s = v ? "ON" : "OFF"; break;
    case RK_CHOICE: s = mg_choices[r->arg].opt[v < mg_choices[r->arg].n ? v : 0u]; break;
    case RK_VOL: put_num(o, v); return;
    case RK_SOON: s = "SOON"; break;
    default: s = ""; break;
    }
    while (*s && n < 15u) o[n++] = *s++;
    o[n] = 0;
}

void settings_view_enter(void)
{
    mg_open(&menu); pull(); dirty = 1;
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
}
int settings_view_input(uint32_t edge)
{
    pull();
    mg_effect_t e = mg_input(&menu, edge, vals);
    dirty = 1;
    switch (e.kind) {
    case EF_SET:
        if (e.id < PREF_COUNT) mp3play_pref_set(e.id, e.value);
        else if (e.id == V_ART) np_art_on = (uint8_t)e.value;
        else if (e.id == V_ACCEPT_ALL) mp3play_accept_all = (uint8_t)e.value;
        pull();
        break;
    case EF_ACTION: return e.id == ACT_INFO ? V_INFO : V_REPORT;
    case EF_CLOSE: return V_NOWPLAYING;
    default: break;
    }
    return VW_STAY;
}
void settings_view_mark(void) { pull(); if (dirty) helios_dirty(R_MENU); }      /* a value changed elsewhere (a button, the Core Settings menu) shows at once */
void settings_view_draw(void)
{
    if (!dirty) return;
    dirty = 0;
    const uint32_t d = menu.depth, pg = menu.page[d], n = mg_rows(&menu), top = menu.top[d];
    gfx_rect(0, 0, GFX_W, TOP_Y, BG);
    const char *title = pg == PG_CHOICE ? mg_pages[menu.page[d - 1u]].rows[menu.sel[d - 1u]].label : mg_pages[pg].title;
    gfx_text(20, 6, title, ACC, BG);
    for (uint32_t k = 0; k < MG_VIS; k++) {
        const uint32_t i = top + k, y = TOP_Y + k * ROW_H;
        if (i >= n) { gfx_rect(0, y, GFX_W, ROW_H, BG); continue; }
        const int on = i == menu.sel[d];
        const uint16_t bg = on ? ACC : BG;
        char v[16]; const char *label;
        if (pg == PG_CHOICE) { label = mg_choices[menu.choice_val[d]].opt[i]; v[0] = vals[menu.choice_val[d]] == i ? '*' : 0; v[1] = 0; }
        else { const mg_row_t *r = &mg_pages[pg].rows[i]; label = r->label; value_text(r, v); }
        const int soon = pg != PG_CHOICE && mg_pages[pg].rows[i].kind == RK_SOON;
        gfx_rect(0, y, GFX_W, ROW_H, bg);
        gfx_text(24, y + 9, label, on ? BG : (soon ? OFFC : FG), bg);
        if (v[0]) gfx_text(GFX_W - 24 - gfx_text_width(v), y + 9, v, on ? BG : (soon ? OFFC : DIM), bg);
    }
    gfx_rect(0, 322, GFX_W, 38, BG);
    gfx_text(20, 322, pg == PG_CHOICE ? "UP DOWN SELECT  A PICK  B BACK" : "UP DOWN SELECT  A OPEN  LEFT RIGHT CHANGE", DIM, BG);
    gfx_text(20, 340, d ? "B BACK  START CLOSE" : "B OR START CLOSE", DIM, BG);
}
