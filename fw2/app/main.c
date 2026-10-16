/* Player 2 S2: boots, checks the bitstream pairing, probes the platform and shows the counter registry. No audio yet (S3). */
#include "../platform/platform.h"
#include "../diagnostics/info_counters.h"
#include "../gfx/gfx.h"
#include "../diagnostics/info_page.h"
#include "../diagnostics/report_page.h"
#include "../aout/aout.h"
#include "../transport/tone.h"
#ifdef P2_MP3PLAY
#include "../settings/settings.h"
#include "../stream/library.h"
#include "../transport/mp3play.h"
#elif defined(P2_LISTPLAY)
#include "../transport/listplay.h"
#endif

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    gfx_rect(0, 0, GFX_W, GFX_H, 0);                      /* clear first: SDRAM powers up with garbage and scanout shows it the moment video is alive */
    platform_probe();
    counters_init();
    counters_aout_init = (uint32_t)aout_init();            /* 0 = the ring is armed; the tone transport only runs then */
#ifdef P2_MP3PLAY
    if (!counters_aout_init) { mp3play_init(caps.cymo); uint32_t sv[SF_COUNT]; settings_init(sv); mp3play_settings_set(sv); }       /* what APF saved last time: volume, repeat, ReplayGain mode */
#elif defined(P2_LISTPLAY)
    listplay_init();                                           /* S4: no audio, the card path only */
#else
    if (!counters_aout_init) tone_init(caps.cymo);
#endif
    info_page_init();
    uint32_t shown_id = 0xFFFFFFFFu;                         /* the title on the header line follows the playing track */
    uint32_t report = 0;                                       /* 1 while the pixel-grid report is on screen */
    uint32_t prev = 0, last_draw = 0, t_prev = cycles(), loops = 0, loop_max = 0;
    counters_refresh(); info_page_draw();
    for (;;) {
        uint32_t t0 = cycles();
        uint32_t keys = REG(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge & KEY_START) { report = 1; report_page_draw(); }                          /* START: snapshot every counter as a pixel grid (one screenshot decodes them all); START again refreshes */
        else if (report && (edge & (KEY_B | KEY_A | KEY_X | KEY_Y | KEY_UP | KEY_DOWN))) { report = 0; info_page_init(); last_draw = 0; }   /* any other key returns to the page */
        else if (!report && edge) { info_page_key(edge
#ifdef P2_MP3PLAY
                             & ~(KEY_UP | KEY_DOWN)                        /* Up/Down jump album in this build; START gives the full report */
#endif
                             );
#ifdef P2_MP3PLAY
            if (!counters_aout_init) mp3play_key(edge);
#elif defined(P2_LISTPLAY)
            listplay_key(edge & ~(KEY_UP | KEY_DOWN));
#else
            if (!counters_aout_init) tone_key(edge);
#endif
            }
#ifdef P2_MP3PLAY
        if (!counters_aout_init) { mp3play_pump(); mp3play_heard();
            { uint32_t sv[SF_COUNT]; mp3play_settings_get(sv); if (settings_service(sv, mp.playing && !mp.held)) mp3play_settings_set(sv); } aout_poll((int)mp.playing); }
#elif defined(P2_LISTPLAY)
        listplay_pump();
#else
        if (!counters_aout_init) { tone_pump(); tone_heard(); aout_poll(tone.playing); }
#endif
#ifdef P2_MP3PLAY
        if (!report && lp.cur_id != shown_id && lp.state == 1) {
            char ttl[40]; shown_id = lp.cur_id; uint32_t n = library_title(shown_id, ttl, sizeof ttl - 1u); ttl[n] = 0;
            gfx_rect(0, 4, GFX_W, 20, RGB565(0, 0, 0)); gfx_text(10, 6, ttl, RGB565(255, 160, 40), RGB565(0, 0, 0));
        }
#endif
        loops++;
        counters_loops = loops; counters_keys = keys;
        if (!report && (edge || (uint32_t)(t0 - last_draw) >= CLK_HZ / 4u)) {
            last_draw = t0;
            counters_refresh();
            info_page_draw();
        }
        uint32_t d = cycles() - t_prev; t_prev = cycles();     /* one whole pass, including the drawing */
        if (d > loop_max) { loop_max = d; counters_loop_max_cyc = d; }
    }
}
