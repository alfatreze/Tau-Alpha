#pragma GCC optimize ("Os")   /* the loop body is light; smaller code is what the 192 KB link needs */
/* Player 2 (the MP3 + FLAC player build): boots, then runs the one loop of spec section 9: input, transport (decode into the ring), settings, and the view on screen. */
#include "../platform/platform.h"
#include "../stream/cold.h"
#include "../diagnostics/info_counters.h"
#include "../gfx/gfx.h"
#include "../diagnostics/info_page.h"
#include "../diagnostics/about_page.h"
#include "../diagnostics/check_page.h"
#include "../diagnostics/report_page.h"
#include "../aout/aout.h"
#include "../settings/settings.h"
#include "../transport/mp3play.h"
#include "../ui/views.h"
#include "../ui/nowplaying.h"
#include "../ui/theme.h"
#include "../ui/meter.h"
#include "../cores/blank_core.h"
#include "../ui/appearance.h"
#include "../ui/settings_view.h"
#include "../aout/halcyon.h"
#include "../stream/assets.h"
#include "../stream/packfile.h"
#include "../gfx/font.h"
#include "../cores/view_core.h"
#include "../../fw/key_repeat.h"

/* Layers that must not include each other meet here (tools/check_deps.py): the app hands each section of tau-assets.bin to its owner, and the diagnostics pages to the views. */
static void hand_out_assets(void)
{
    uint32_t total; const uint8_t *base = assets_file(&total); const uint8_t *p; uint32_t n;
    uint32_t t0 = cycles();
#ifdef P2_PACKS
    /* The meter packs first (they do not depend on tau-assets.bin): the core's own tau-packs.bin (data slot 9, shipped with the release) wins, else the MPKB section of tau-assets.bin (a card prepared by hand);
     * the pack slots live in the PSRAM window: proven first. */
    int packs_done = 0;
    if (assets_psram_ok()) packs_done = meters_packs_load_file(packfile_rd, 0);
    assets_boot.packs = cycles() - t0; t0 = cycles();
    if (!base) return;
#else
    if (!base) return;
#endif
    theme_file_load(base, total);                       /* THEM: extra themes (a bad section leaves the built-ins and a code for Info) */
    assets_boot.themes = cycles() - t0; t0 = cycles();
    if (assets_section("PRST", &p, &n)) halcyon_prst_load(p, n, assets_in_psram(), assets_psram_ok);
    assets_boot.prst = cycles() - t0; t0 = cycles();
#ifdef P2_PACKS
    if (!packs_done && assets_section("MPKB", &p, &n) && (!assets_in_psram() || assets_psram_ok()) && assets_psram_ok()) { meters_packs_load(p, n); assets_boot.packs = cycles() - t0; }
    t0 = cycles();
#endif
    if (assets_section("METR", &p, &n) && (!assets_in_psram() || assets_psram_ok())) meters_metr_load(p, n);       /* a file over 4 KiB sits in the PSRAM staging area: proven first, as for PRST */                              /* user presets replace the built-in ones of the meters the file carries */
    assets_boot.presets = cycles() - t0;
}
static void info_draw_all(void) { counters_refresh(); info_page_draw(); }
static const views_diag_t diag_pages = { info_page_init, info_page_key, info_draw_all, report_page_draw, check_page_enter, check_page_key, check_page_tick, check_page_draw, check_page_dirty, check_page_exit, check_page_repaint, report_page_set_payload, about_page_init, about_page_key, about_page_draw };

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    gfx_rect(0, 0, GFX_W, GFX_H, 0);
    platform_probe();
    { int ce = cold_boot();                                    /* the UI, library and settings run from the cold image: without it nothing can be shown, so say why and stop */
      if (ce) {
          char m[24] = "COLD IMAGE FAILED E00"; m[19] = (char)('0' + (ce / 10) % 10); m[20] = (char)('0' + ce % 10);
          gfx_text(10, 10, m, RGB565(255, 160, 40), 0); gfx_text(10, 40, "TAU-COLD.BIN MISSING OR WRONG BUILD", RGB565(160, 160, 160), 0);
          for (;;) { }
      } }
    counters_init();
    counters_aout_init = (uint32_t)aout_init();
    halcyon_init();
    halcyon_set_dip(mp3play_dip_begin, mp3play_dip_end);
    meters_init();                                                                          /* before the assets and the saved settings: both set meter presets */
    theme_init();                                                                           /* BEFORE the saved settings are applied: it resets theme, mode and colour to the defaults, so after them it threw the restored colour away and the next settings service published the default over the saved one */
    if (!counters_aout_init) { mp3play_init(caps.cymo); assets_load(); hand_out_assets(); uint32_t sv[SF_COUNT]; settings_init(sv); mp3play_settings_set(sv); appearance_set(sv); mp3play_last_restore(&sv[SF_LAST0]); settings_view_init_choices(); }       /* the assets (user Halcyon presets) are read before the saved preset is applied */
    views_set_diag(&diag_pages);
    views_init();
    kr_t kr_pad = { 0 }, kr_vol = { 0 };
    uint32_t sel_used = 0, st_used = 0, lr_key = 0, lr_t0 = 0, lr_next = 0, lr_seeked = 0;
    bl_t blank = { 0 };
    uint32_t prev = 0, t_prev = cycles(), loops = 0, loop_max = 0;
    for (;;) {
        uint32_t t0 = cycles();
        uint32_t keys = REG(R_INPUT) & 0xFFFFu, edge = keys & ~prev, rel = prev & ~keys; prev = keys;
        uint32_t now = cycles();
        blank.sel = blank_sel;
        { const int bs = bl_step(&blank, now, CLK_HZ, edge | rel);
          if (bs == BL_SLEEP) gfx_rect(0, 0, GFX_W, GFX_H, 0);                              /* black: playback goes on, nothing is drawn */
          if (bs == BL_WAKE) views_refresh();                                               /* the first press wakes the screen and is swallowed */
          if (bs == BL_WAKE || blank.blanked) { edge = 0; rel = 0; keys = 0; } }
        const uint32_t cur = views_cur(), np_like = (cur == V_NOWPLAYING || cur == V_FULLSCREEN);
        { uint32_t rep = 0;
          if (cur == V_LIBRARY || cur == V_SETTINGS || cur == V_INFO || cur == V_CONFIGURE || cur == V_CONTROLS || cur == V_HELP || cur == V_ABOUT) edge = kr_step(&kr_pad, edge, keys, KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT, now, CLK_HZ * 2u / 5u, CLK_HZ / 14u, &rep);      /* hold a direction to repeat */
          else if (np_like && !(keys & KEY_SELECT)) edge = kr_step(&kr_vol, edge, keys, KEY_UP | KEY_DOWN, now, CLK_HZ * 2u / 5u, CLK_HZ / 14u, &rep);                                  /* hold Up/Down for volume */
        }
        /* Select and Start are taps (acted on at release, and not at all if another key was pressed while they were down) or modifiers. */
        if (edge & KEY_SELECT) sel_used = 0;
        if (edge & KEY_START) st_used = 0;
        if ((keys & KEY_SELECT) && (edge & ~KEY_SELECT)) sel_used = 1;
        if ((keys & KEY_START) && (edge & ~KEY_START)) st_used = 1;
        uint32_t lr_dir = 0;                                                                 /* Left/Right on Now Playing: a tap changes track, a hold seeks (5 s, then 10 s, then 30 s a step) */
        if (np_like && !(keys & (KEY_SELECT | KEY_START))) {
            if (!lr_key && (edge & (KEY_LEFT | KEY_RIGHT))) { lr_key = edge & (KEY_LEFT | KEY_RIGHT); lr_t0 = now; lr_next = now + CLK_HZ * 2u / 5u; lr_seeked = 0; }
            if (lr_key && (keys & lr_key) && (int32_t)(now - lr_next) >= 0) {
                uint32_t held = now - lr_t0; int32_t st = held < CLK_HZ * 2u ? 5 : held < CLK_HZ * 4u ? 10 : 30;
                mp3play_act(MPA_SEEK, (lr_key & KEY_LEFT) ? -st : st); lr_seeked = 1; lr_next += CLK_HZ / 4u;
            }
        }
        if (lr_key && (rel & lr_key)) { if (!lr_seeked && np_like) lr_dir = lr_key; lr_key = 0; }
        else if (lr_key && !(keys & lr_key)) lr_key = 0;
        if (((edge & KEY_START) && (keys & KEY_SELECT)) || ((edge & KEY_SELECT) && (keys & KEY_START))) { if (cur == V_REPORT) views_report_refresh(); else views_switch(V_REPORT); }      /* Select+Start: one screenshot decodes every counter; again refreshes */
        else if (cur == V_REPORT) { if (edge) views_input(edge); }
        else if ((rel & KEY_START) && !st_used) views_switch(cur == V_SETTINGS || cur == V_LIBRARY || cur == V_HALCYON || cur == V_CONFIGURE || cur == V_CONTROLS || cur == V_HELP || cur == V_ABOUT ? V_NOWPLAYING : V_SETTINGS);     /* Start: Settings (closes Settings or the library) */
        else if ((rel & KEY_SELECT) && !sel_used) {                                           /* Select: the library (back a level inside it) */
            if (cur == V_LIBRARY) views_input(KEY_B); else views_switch(np_like ? (nowplaying_fallback() ? V_HELP : V_LIBRARY) : V_NOWPLAYING);
        }
        else if ((keys & KEY_SELECT) && (edge & KEY_B)) views_switch(np_like ? V_INFO : V_NOWPLAYING);
        else if (np_like) {
            if ((keys & KEY_START) && (edge & KEY_X)) views_switch(V_CONFIGURE);                  /* Start+X: the Configure page of the current meter */
            else if (keys & KEY_SELECT) {                                                     /* Select + ... */
                if (edge & KEY_UP) mp3play_act(MPA_ALBUM, -1);
                if (edge & KEY_DOWN) mp3play_act(MPA_ALBUM, 1);
                if (edge & KEY_LEFT) mp3play_act(MPA_SEEK, -1);
                if (edge & KEY_RIGHT) mp3play_act(MPA_SEEK, 1);
                if (edge & KEY_L1) mp3play_act(MPA_REPEAT, 0);
                if (edge & KEY_Y) views_switch(cur == V_FULLSCREEN ? V_NOWPLAYING : V_FULLSCREEN);       /* Select+Y: the fullscreen meter */
                if (edge & KEY_X) meters_preset_next();                                                    /* Select+X: the next preset of this meter */
            } else {
                if (lr_dir) mp3play_act(lr_dir & KEY_RIGHT ? MPA_NEXT : MPA_PREV, 0);
                if (edge) views_input(edge);
            }
        }
        else if (edge) views_input(edge);
        if (!counters_aout_init) {
            mp3play_pump(); mp3play_heard();
            uint32_t sv[SF_COUNT]; settings_seed(sv); mp3play_settings_get(sv); appearance_get(sv); mp3play_last_get(&sv[SF_LAST0]); if (settings_service(sv, mp.playing && !mp.held)) { mp3play_settings_set(sv); appearance_set(sv); }
            aout_poll((int)mp.playing);
        }
        loops++; counters_loops = loops; counters_keys = keys;
        if (th_changed || font_changed) { th_changed = 0; font_changed = 0; views_refresh(); }                             /* a colour, theme or mode change repaints the view in place */
        if (!blank.blanked) { views_mark(); views_flush(); }                                                       /* the view marks what changed; the dirty regions the beam is not in are drawn */
        uint32_t d = cycles() - t_prev; t_prev = cycles();
        if (d > loop_max) { loop_max = d; counters_loop_max_cyc = d; }
    }
}
