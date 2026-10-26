/* The built-in Layered Wave (fw2/ui/meter_layered_wave.c compiled straight in, as in the firmware) run over the shared trace of sim/p2_pack_trace.h under tools/rv32sim.py: the other half of the comparison with
 * tools/host/p2_pack_harness.c (the same meter as a pack). Built by tools/player2/measure_pack_cost.py with -DTR_NOHASH so both sides pay the same few instructions per engine call. Prints "S <scenario> <commands>" lines. */
#include "hostio.h"
#include "../../fw2/ui/meter.h"
#include "../../fw2/ui/theme.h"
#include "../../fw2/generated/meter_presets.h"
#include "../../fw2/generated/lw_presets.h"
static uint32_t lw_state_dummy[700];
#define __psram_state_start lw_state_dummy
#define __psram_state_end (lw_state_dummy + 700)
#include "../../fw2/ui/meter_layered_wave.c"
#include "../../sim/p2_pack_trace.h"
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { tr_hash_rect(x, y, w, h, c); }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { return tr_hash_text(x, y, s, fg, bg); }
static void report(int s, uint32_t n, uint64_t h) { (void)h; hputs("S "); hputu((uint32_t)s); hputc(' '); hputu(n); hnl(); }
static void lw_noapply(void) { }
static void run(const mtr_in_t *in) { meter_layered_wave_tick(in); }
int main(void)
{
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    lw_trace(meter_layered_wave_mv, lw_noapply, run, report);
    return 0;
}
