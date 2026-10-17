#pragma GCC optimize ("Os")
#include "settings_view.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../transport/mp3play.h"
#include "../aout/aout.h"
#include "../gfx/font.h"
#include "../cores/menupages_core.h"
#include "../cores/view_core.h"
#include "nowplaying.h"
#include "views.h"
#include "regions.h"
#include "theme.h"
#include "meter.h"
#include "../platform/platform.h"
#include "helios.h"
#include "xfade.h"

#define BG   UI_BG_SCREEN
#define FG   UI_WHITE
#define DIM  UI_DIM
#define OFFC UI_FAINT
#define ACC  UI_ACCENT
#define ROW_H 34u
#define TOP_Y 34u
/* The menu's value ids 0..6 are the transport's preferences. */
_Static_assert(V_VOLUME == PREF_VOLUME && V_RG == PREF_RG && V_STRETCH == PREF_STRETCH && V_REPEAT == PREF_REPEAT && V_SHUFFLE == PREF_SHUFFLE && V_SEEKSTEP == PREF_SEEKSTEP && V_SKIPSIL == PREF_SKIPSIL && V_SPEED == PREF_SPEED, "menu value ids must mirror PREF_*");

uint8_t blank_sel;                       /* Settings > Appearance > Screen blank: an index into bl_minutes (not persisted, as in the old player) */
static mg_t menu; static uint32_t vals[V_COUNT], dirty; static uint8_t resume;      /* 1: coming back from Info, Report or the Halcyon page: keep the page you were on */

static void pull(void)
{
    const uint32_t cur_vals[4] = { th_pal, th_theme, th_pol, meters_cur() };
    for (uint32_t k = 0; k < 4u; k++) if (vals[V_COLOUR + k] != cur_vals[k]) { vals[V_COLOUR + k] = cur_vals[k]; dirty = 1; }
    for (uint32_t i = 0; i < PREF_COUNT; i++) { uint32_t v = mp3play_pref_get(i); if (v != vals[i]) { vals[i] = v; dirty = 1; } }
    mg_speed_for_mode(vals[V_STRETCH]);                           /* the speed list follows the mode */
    if (vals[V_ART] != np_art_on) { vals[V_ART] = np_art_on; dirty = 1; }
    { const uint32_t cy = aout_cymo_user_is_off() ? 0u : 1u, a16 = mp3play_a16_get(); if (vals[V_CYMO] != cy) { vals[V_CYMO] = cy; dirty = 1; } if (vals[V_A16] != a16) { vals[V_A16] = a16; dirty = 1; } }
    if (vals[V_BLANK] != blank_sel) { vals[V_BLANK] = blank_sel; dirty = 1; }
    if (vals[V_FONT] != font_cur) { vals[V_FONT] = font_cur; dirty = 1; }
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

/* A change of page (opening a group, going back, opening or leaving an option list) is shown as a crossfade; moving the selection or changing a value inside a page is instant. */
static uint32_t shown_sig = 0xFFFFFFFFu;
static uint32_t page_sig(void) { const uint32_t d = menu.depth, pg = menu.page[d]; return d * 1024u + pg * 32u + (pg == PG_CHOICE && d ? menu.sel[d - 1u] : 0u); }
void settings_view_exit(void) { xfade_finish(); }
void settings_view_enter(void)
{
    xfade_finish(); shown_sig = 0xFFFFFFFFu;
    if (!resume) mg_open(&menu);
    resume = 0; pull(); dirty = 1;
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
}
int settings_view_input(uint32_t edge)
{
    xfade_finish();                                   /* a key while a fade runs: the fade is completed first, then the key acts on the finished page */
    pull();
    mg_effect_t e = mg_input(&menu, edge, vals);
    dirty = 1;
    switch (e.kind) {
    case EF_SET:
        if (e.id < PREF_COUNT) mp3play_pref_set(e.id, e.value);
        else if (e.id == V_ART) np_art_on = (uint8_t)e.value;
        else if (e.id == V_ACCEPT_ALL) mp3play_accept_all = (uint8_t)e.value;
        else if (e.id == V_COLOUR) theme_set(th_theme, th_pol, e.value);
        else if (e.id == V_THEME) theme_set(e.value, th_pol, th_pal);
        else if (e.id == V_MODE) theme_set(th_theme, e.value, th_pal);
        else if (e.id == V_METER) meters_select(e.value);
        else if (e.id == V_BLANK) blank_sel = (uint8_t)e.value;
        else if (e.id == V_FONT) font_select(e.value);                 /* loads the font into the engine; the main loop repaints the view once (font_changed) */
        else if (e.id == V_CYMO) aout_cymo_user_off(e.value ? 0 : 1);
        else if (e.id == V_A16) mp3play_a16_set(e.value);
        pull();
        break;
    case EF_ACTION: resume = 1; return e.id == ACT_INFO ? V_INFO : e.id == ACT_HALCYON ? V_HALCYON : e.id == ACT_CONFIGURE ? V_CONFIGURE : e.id == ACT_CHECK ? V_CHECK : V_REPORT;
    case EF_CLOSE: return V_NOWPLAYING;
    default: break;
    }
    return VW_STAY;
}
void settings_view_mark(void) { pull(); if (dirty) helios_dirty(R_MENU); xfade_tick(); }      /* a value changed elsewhere (a button, the Core Settings menu) shows at once */
void settings_view_draw(void)
{
    if (!dirty) return;
    dirty = 0;
    { const uint32_t sg = page_sig(); if (sg != shown_sig) { if (shown_sig != 0xFFFFFFFFu) xfade_request(); shown_sig = sg; } }
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
        gfx_text(24, y + 9, label, on ? UI_ON_ACCENT : (soon ? OFFC : FG), bg);
        if (v[0]) gfx_text(GFX_W - 24 - gfx_text_width(v), y + 9, v, on ? UI_ON_ACCENT : (soon ? OFFC : DIM), bg);
    }
    gfx_rect(0, 322, GFX_W, 38, BG);
    gfx_text(20, 322, pg == PG_CHOICE ? "UP DOWN  A PICK  B BACK" : "UP DOWN  A OPEN  LEFT RIGHT CHANGE", DIM, BG);
    gfx_text(20, 340, d ? "B BACK  START CLOSE" : "B OR START CLOSE", DIM, BG);
}

/* The option lists that depend on the build and on tau-assets.bin: the colours, the themes (built-in then file themes) and the meters. Called once at boot after the assets are read. */
void settings_view_init_choices(void)
{
    static const char *theme_names[8], *meter_names[8];
    uint32_t n = theme_count(); if (n > 8u) n = 8u;
    for (uint32_t i = 0; i < n; i++) theme_names[i] = theme_name(i);
    uint32_t m = meter_count > 8u ? 8u : meter_count;
    for (uint32_t i = 0; i < m; i++) meter_names[i] = meter_table[i].name;
    mg_set_choice(V_COLOUR, th_palette_name, 19u);
    mg_set_choice(V_THEME, theme_names, n);
    mg_set_choice(V_METER, meter_names, m);
}
void settings_view_keep(void) { resume = 1u; }
void settings_view_redraw(void) { xfade_finish(); gfx_rect(0, 0, GFX_W, GFX_H, BG); dirty = 1; pull(); }
