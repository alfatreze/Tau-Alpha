/* Player 2 (the MP3 + FLAC player build): boots, then runs the one loop of spec section 9: input, transport (decode into the ring), settings, and the view on screen. */
#include "../platform/platform.h"
#include "../diagnostics/info_counters.h"
#include "../gfx/gfx.h"
#include "../diagnostics/info_page.h"
#include "../diagnostics/report_page.h"
#include "../aout/aout.h"
#include "../settings/settings.h"
#include "../transport/mp3play.h"
#include "../ui/nowplaying.h"
#include "../ui/browser_view.h"
#include "../ui/settings_view.h"
#include "../../fw/key_repeat.h"
#include "../transport/browser.h"

enum { V_NOWPLAYING = 0, V_INFO = 1, V_REPORT = 2, V_LIBRARY = 3, V_SETTINGS = 4 };
static void enter_view(uint32_t v) { if (v == V_INFO) info_page_init(); else if (v == V_LIBRARY) { browser_enter(); browser_view_enter(); } else if (v == V_SETTINGS) settings_view_enter(); else nowplaying_enter(); }

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    gfx_rect(0, 0, GFX_W, GFX_H, 0);
    platform_probe();
    counters_init();
    counters_aout_init = (uint32_t)aout_init();
    if (!counters_aout_init) { mp3play_init(caps.cymo); uint32_t sv[SF_COUNT]; settings_init(sv); mp3play_settings_set(sv); }
    uint32_t view = V_NOWPLAYING, back_to = V_NOWPLAYING;
    nowplaying_enter();
    kr_t kr_pad = { 0 }, kr_vol = { 0 };
    uint32_t prev = 0, last_draw = 0, t_prev = cycles(), loops = 0, loop_max = 0;
    for (;;) {
        uint32_t t0 = cycles();
        uint32_t keys = REG(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        { uint32_t now = cycles(), rep = 0;
          if (view == V_LIBRARY || view == V_SETTINGS || view == V_INFO) edge = kr_step(&kr_pad, edge, keys, KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT, now, CLK_HZ * 2u / 5u, CLK_HZ / 14u, &rep);      /* hold a direction to repeat */
          else if (view == V_NOWPLAYING && !(keys & KEY_SELECT)) edge = kr_step(&kr_vol, edge, keys, KEY_L1 | KEY_R1, now, CLK_HZ * 2u / 5u, CLK_HZ / 14u, &rep);                                        /* hold L1/R1 for volume */
        }
        if (edge & KEY_START) { if (view != V_REPORT) back_to = view; view = V_REPORT; report_page_draw(); }      /* one screenshot decodes every counter; START again refreshes */
        else if (view == V_REPORT) { if (edge & (KEY_B | KEY_A | KEY_X | KEY_Y | KEY_UP | KEY_DOWN)) { view = back_to; enter_view(view); last_draw = 0; } }
        else if ((keys & KEY_SELECT) && (edge & KEY_A)) { view = view == V_LIBRARY ? V_NOWPLAYING : V_LIBRARY; enter_view(view); last_draw = 0; }        /* Select+A: the library browser */
        else if ((keys & KEY_SELECT) && (edge & KEY_UP)) { view = view == V_SETTINGS ? V_NOWPLAYING : V_SETTINGS; enter_view(view); last_draw = 0; }        /* Select+Up: Settings */
        else if (view == V_SETTINGS && edge) { if (edge & KEY_B) { view = V_NOWPLAYING; enter_view(view); } else settings_view_key(edge); }
        else if (view == V_LIBRARY && edge) { if (browser_key(edge)) { view = V_NOWPLAYING; enter_view(view); } }
        else if ((keys & KEY_SELECT) && (edge & KEY_B)) { view = view == V_NOWPLAYING ? V_INFO : V_NOWPLAYING; enter_view(view); last_draw = 0; }
        else if (edge) {
            if (view == V_INFO) info_page_key(edge & (KEY_UP | KEY_DOWN));
            if (!counters_aout_init) mp3play_key(view == V_INFO ? (edge & ~(KEY_UP | KEY_DOWN)) : edge);        /* Up/Down scroll the Info page; on Now Playing they jump album */
        }
        if (!counters_aout_init) {
            mp3play_pump(); mp3play_heard();
            uint32_t sv[SF_COUNT]; mp3play_settings_get(sv); if (settings_service(sv, mp.playing && !mp.held)) mp3play_settings_set(sv);
            aout_poll((int)mp.playing);
        }
        loops++; counters_loops = loops; counters_keys = keys;
        if (view == V_NOWPLAYING) nowplaying_draw();
        else if (view == V_LIBRARY) browser_view_draw();
        else if (view == V_SETTINGS) settings_view_draw();
        else if (view == V_INFO && (edge || (uint32_t)(t0 - last_draw) >= CLK_HZ / 4u)) { last_draw = t0; counters_refresh(); info_page_draw(); }
        uint32_t d = cycles() - t_prev; t_prev = cycles();
        if (d > loop_max) { loop_max = d; counters_loop_max_cyc = d; }
    }
}
