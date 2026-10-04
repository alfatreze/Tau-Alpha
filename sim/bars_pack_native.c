/* Native build of the in-tree Winamp Bars (fw/winamp_bars.inc), run over the shared trace of sim/bars_pack_trace.h; prints "S <scenario> <commands> <hash>". */
#include <stdint.h>
#include <stdio.h>
#define COLD_FN3
#define COLD_DATA
#define WAVE_COLS 64u
#define SPEC_BANDS 16u
#define WVIZ_BANDS_MIN 4u
#define WVIZ_BANDS_MAX 16u
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#include "meters_gen.h"
#include "meter_core.h"
static uint16_t ui_accent;
static uint8_t wviz_force;
static void bp_hash_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static void bp_hash_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg);
#undef fig_rect
#undef fig_bar
#define fig_rect bp_hash_rect
#define fig_bar  bp_hash_bar
#define blit_probe_ensure() ((void)0)
#define BLIT_READY() 1
#include "winamp_bars.inc"
#include "bars_pack_trace.h"
static uint32_t run(const mtr_in_t *in) { wviz_bars_tick(in); return 1u; }
static void report(int s, uint32_t n, uint64_t h) { printf("S %d %u %016llx\n", s, n, (unsigned long long)h); }
int main(void)
{
    ui_accent = 0x3C0Fu;
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    bars_trace(mtr_v_winamp_bars, &wviz_force, run, report);
    return 0;
}
