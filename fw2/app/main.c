/* Player 2 S2: boots, checks the bitstream pairing, probes the platform and shows the counter registry. No audio yet (S3). */
#include "../platform/platform.h"
#include "../diagnostics/info_counters.h"
#include "../gfx/gfx.h"
#include "../diagnostics/info_page.h"
#include "../diagnostics/report_page.h"
#include "../aout/aout.h"
#include "../transport/tone.h"

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    gfx_rect(0, 0, GFX_W, GFX_H, 0);                      /* clear first: SDRAM powers up with garbage and scanout shows it the moment video is alive */
    platform_probe();
    counters_init();
    counters_aout_init = (uint32_t)aout_init();            /* 0 = the ring is armed; the tone transport only runs then */
    if (!counters_aout_init) tone_init(caps.cymo);
    info_page_init();
    uint32_t report = 0;                                       /* 1 while the pixel-grid report is on screen */
    uint32_t prev = 0, last_draw = 0, t_prev = cycles(), loops = 0, loop_max = 0;
    counters_refresh(); info_page_draw();
    for (;;) {
        uint32_t t0 = cycles();
        uint32_t keys = REG(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge & KEY_START) { report = 1; report_page_draw(); }                          /* START: snapshot every counter as a pixel grid (one screenshot decodes them all); START again refreshes */
        else if (report && (edge & (KEY_B | KEY_A | KEY_X | KEY_Y | KEY_UP | KEY_DOWN))) { report = 0; info_page_init(); last_draw = 0; }   /* any other key returns to the page */
        else if (!report && edge) { info_page_key(edge); if (!counters_aout_init) tone_key(edge); }
        if (!counters_aout_init) { tone_pump(); tone_heard(); aout_poll(tone.playing); }
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
