/* Native build of the in-tree Layered Wave (fw/layered_wave.inc), run over the shared trace of sim/lw_pack_trace.h; prints "S <scenario> <commands> <hash>". */
#include <stdint.h>
#include <stdio.h>
#define COLD_FN3
#define LW_STATS 1        /* as the firmware and the pack: statistics and the time-based cost guard (cycles() is 0 here, the same on both sides) */
#define CLK_HZ 66666667u
#define COLD_DATA
#define WAVE_COLS 64u
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#include "meters_gen.h"
#include "meter_core.h"
static uint16_t ui_accent;
static uint8_t wviz_force;
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static uint32_t cycles(void) { return 0u; }
#include "layered_wave.inc"
#include "lw_pack_trace.h"
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { lw_hash_rect(x, y, w, h, c); }
static uint32_t run(const mtr_in_t *in) { return (uint32_t)lw_tick(in); }
static void report(int s, uint32_t n, uint64_t h) { printf("S %d %u %016llx\n", s, n, (unsigned long long)h); }
int main(void)
{
    ui_accent = 0x3C0Fu;
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    lw_trace(mtr_v_layered_wave, &wviz_force, run, report);
    printf("STATS %u %u\n", (unsigned)lw_cmd_last, (unsigned)lw_stride);
    return 0;
}
