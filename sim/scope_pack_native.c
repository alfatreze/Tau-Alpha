/* Native build of the in-tree Winamp Scope (fw/winamp_scope.inc), run over the shared trace of sim/scope_pack_trace.h; prints "S <scenario> <commands> <hash>". */
#include <stdint.h>
#include <stdio.h>
#define COLD_FN3
#define COLD_DATA
#define MTR_PACK 1
#define WAVE_COLS 64u
#define WVIZ_SCOPE_Y_N 64u
#define SCOPE_UNIT 100
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#include "meters_gen.h"
#include "meter_core.h"
static uint16_t ui_accent;
static uint8_t wviz_force, ui_fullscreen, sp_grad;
static void sp_hash_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static void sp_hash_restore(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
static int  sp_hash_blend(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t a);
static void sp_hash_note(int ok, uint32_t x, uint32_t y);
#undef fig_rect
#define fig_rect sp_hash_rect
#define ui_bg_restore sp_hash_restore
#define ui_bg_blend sp_hash_blend
#define SCOPE_NOTE(ok, px, py) sp_hash_note((ok), (px), (py))
#include "winamp_scope.inc"
#include "scope_pack_trace.h"
static uint32_t run(const mtr_in_t *in) { wviz_scope_tick(in, sp_grad); return 1u; }
static void report(int s, uint32_t n, uint64_t h) { printf("S %d %u %016llx\n", s, n, (unsigned long long)h); }
int main(void)
{
    ui_accent = 0x3C0Fu;
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    scope_trace(mtr_v_winamp_scope, &wviz_force, &sp_grad, &ui_fullscreen, run, report);
    return 0;
}
