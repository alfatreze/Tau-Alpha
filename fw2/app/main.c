/* Player 2 S2: boots, checks the bitstream pairing, probes the platform and shows the counter registry. No audio yet (S3). */
#include "../platform/platform.h"
#include "../diagnostics/info_counters.h"
#include "../gfx/gfx.h"
#include "../diagnostics/info_page.h"

int main(void)
{
    if (!platform_version_ok()) platform_halt_pattern();
    gfx_rect(0, 0, GFX_W, GFX_H, 0);                      /* clear first: SDRAM powers up with garbage and scanout shows it the moment video is alive */
    platform_probe();
    counters_init();
    info_page_init();
    uint32_t prev = 0, last_draw = 0, t_prev = cycles(), loops = 0, loop_max = 0;
    counters_refresh(); info_page_draw();
    for (;;) {
        uint32_t t0 = cycles();
        uint32_t keys = REG(R_INPUT) & 0xFFFFu, edge = keys & ~prev; prev = keys;
        if (edge) info_page_key(edge);
        loops++;
        counters_loops = loops; counters_keys = keys;
        if (edge || (uint32_t)(t0 - last_draw) >= CLK_HZ / 4u) {
            last_draw = t0;
            counters_refresh();
            info_page_draw();
        }
        uint32_t d = cycles() - t_prev; t_prev = cycles();     /* one whole pass, including the drawing */
        if (d > loop_max) { loop_max = d; counters_loop_max_cyc = d; }
    }
}
