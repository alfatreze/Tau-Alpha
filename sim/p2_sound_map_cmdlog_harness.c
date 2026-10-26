/* SOUND MAP's drawing source (fw2/ui/meter_sound_map.c, under P2_PACK) run natively with every draw command printed, for the lab twin's parity test (sim/test_sound_map_twin.py). stdin, one line each:
 *   R r0 .. r20                                             the theme roles (RGB565), index = the TR_* enum
 *   T mode gain cell span x y w h bg force paused degrade frame dt_ms peak_l peak_r rms_l rms_r corr centroid crest flux onset stats_ok s0 .. s15     one picture
 * stdout: "T n" then one line per command as the lab's framebuffer logs it: "rect x y w h c", "bar x y w h lit fg bg", "text x y s fg bg". */
#include <stdio.h>
#include <stdint.h>
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { printf("rect %u %u %u %u %u\n", x, y, w, h, c); }
void gfx_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg) { printf("bar %u %u %u %u %u %u %u\n", x, y, w, h, lit, fg, bg); }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { printf("text %u %u %s %u %u\n", x, y, s, fg, bg); return 0; }
uint16_t th_role[TR_COUNT];
#define P2_PACK 1
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#include "meter_sound_map.c"
int main(void)
{
    char c; int n = 0;
    while (scanf(" %c", &c) == 1) {
        if (c == 'R') { for (int i = 0; i < TR_COUNT; i++) { unsigned v; scanf("%u", &v); th_role[i] = (uint16_t)v; } continue; }
        unsigned mode, gain, cell, span, x, y, w, h, bg, force, paused, degrade, frame, dt, pl, pr, rl, rr, cen, cr, fl, on, ok, sp[16]; int corr;
        scanf("%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d %u %u %u %u %u", &mode, &gain, &cell, &span, &x, &y, &w, &h, &bg, &force, &paused, &degrade, &frame, &dt, &pl, &pr, &rl, &rr, &corr, &cen, &cr, &fl, &on, &ok);
        uint8_t spec[16]; for (int i = 0; i < 16; i++) { scanf("%u", &sp[i]); spec[i] = (uint8_t)sp[i]; }
        uint16_t pv[4] = { (uint16_t)mode, (uint16_t)gain, (uint16_t)cell, (uint16_t)span }; smp_load(pv);
        mtr_in_t in = {0}; in.spec = spec; in.x = (uint16_t)x; in.y = (uint16_t)y; in.w = (uint16_t)w; in.h = (uint16_t)h; in.bg = (uint16_t)bg; in.force = (uint8_t)force; in.paused = (uint8_t)paused; in.degrade = (uint8_t)degrade;
        in.frame = frame; in.dt_ms = (uint16_t)dt; in.peak_l = pl; in.peak_r = pr; in.rms_l = (uint16_t)rl; in.rms_r = (uint16_t)rr; in.corr = (int16_t)corr; in.centroid = (uint8_t)cen; in.crest = (uint8_t)cr; in.flux = (uint8_t)fl; in.onset = (uint8_t)on; in.stats_ok = (uint8_t)ok;
        printf("T %d\n", n++); smp_tick(&in);
    }
    return 0;
}
