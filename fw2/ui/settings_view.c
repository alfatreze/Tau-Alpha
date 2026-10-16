#pragma GCC optimize ("Os")
#include "settings_view.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../transport/mp3play.h"
#include "../cores/menu_core.h"

#define BG   RGB565(0, 0, 0)
#define FG   RGB565(255, 255, 255)
#define DIM  RGB565(140, 146, 160)
#define ACC  RGB565(255, 160, 40)
#define ROW_H 34u
#define TOP_Y 40u
static const mi_t items[PREF_COUNT] = { {0, 0, 100, 1}, {1, 0, 2, 1}, {1, 0, 7, 1}, {1, 0, 2, 1}, {1, 0, 1, 1}, {1, 0, 3, 1} };
static const char *const names[PREF_COUNT] = { "VOLUME", "REPLAYGAIN", "TEMPO", "REPEAT", "SHUFFLE", "SEEK STEP" };
static menu_t menu; static uint32_t vals[PREF_COUNT], shown[PREF_COUNT], shown_sel = 0xFFFFFFFFu, dirty;

static void value_text(uint32_t i, char *o)
{
    static const char *const rg[3] = { "OFF", "TRACK", "ALBUM" }, *const sp[8] = { "0.75X", "0.85X", "1.00X", "1.10X", "1.25X", "1.50X", "1.75X", "2.00X" },
                      *const rp[3] = { "OFF", "ALL", "ONE" }, *const sk[4] = { "10 S", "30 S", "60 S", "120 S" };
    const char *s = 0; uint32_t v = vals[i], n = 0;
    switch (i) { case PREF_RG: s = rg[v % 3u]; break; case PREF_TEMPO: s = sp[v & 7u]; break; case PREF_REPEAT: s = rp[v % 3u]; break; case PREF_SHUFFLE: s = v ? "ON" : "OFF"; break; case PREF_SEEKSTEP: s = sk[v & 3u]; break; }
    if (s) { while (*s && n < 15u) o[n++] = *s++; o[n] = 0; return; }
    char t[8]; uint32_t k = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) o[n++] = t[--k]; o[n] = 0;
}

static void pull(void) { for (uint32_t i = 0; i < PREF_COUNT; i++) vals[i] = mp3play_pref_get(i); }
void settings_view_enter(void)
{
    if (!menu.n) menu_init(&menu, PREF_COUNT, 8);
    pull(); gfx_rect(0, 0, GFX_W, GFX_H, BG);
    gfx_text(20, 6, "SETTINGS", ACC, BG);
    gfx_text(20, 322, "UP DOWN SELECT  LEFT RIGHT CHANGE", DIM, BG);
    gfx_text(20, 340, "B BACK", DIM, BG);
    dirty = 1; shown_sel = 0xFFFFFFFFu;
}
void settings_view_key(uint32_t edge)
{
    if (edge & KEY_DOWN) menu_move(&menu, 1);
    if (edge & KEY_UP) menu_move(&menu, -1);
    int d = (edge & KEY_RIGHT) ? 1 : (edge & KEY_LEFT) ? -1 : 0;
    if (d && menu_adjust(&menu, items, vals, d)) mp3play_pref_set(menu.sel, vals[menu.sel]);
    dirty = 1;
}
void settings_view_draw(void)
{
    uint32_t cur[PREF_COUNT]; for (uint32_t i = 0; i < PREF_COUNT; i++) cur[i] = mp3play_pref_get(i);
    for (uint32_t i = 0; i < PREF_COUNT; i++) if (cur[i] != vals[i]) { vals[i] = cur[i]; dirty = 1; }       /* changed elsewhere (a button, the Core Settings menu) */
    if (!dirty) return;
    dirty = 0;
    for (uint32_t i = 0; i < PREF_COUNT; i++) {
        uint32_t y = TOP_Y + i * ROW_H; int on = i == menu.sel;
        char v[16]; value_text(i, v);
        gfx_rect(0, y, GFX_W, ROW_H, on ? ACC : BG);
        gfx_text(24, y + 9, names[i], on ? BG : FG, on ? ACC : BG);
        gfx_text(GFX_W - 24 - gfx_text_width(v), y + 9, v, on ? BG : DIM, on ? ACC : BG);
        shown[i] = vals[i];
    }
    (void)shown; (void)shown_sel;
}
