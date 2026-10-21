/* The reference: fw2/ui/meter_winamp_bars.c compiled straight in and run over the pack trace (sim/p2_bars_pack_trace.h). Prints "S <scenario> <commands> <hash>" lines. */
#include <stdio.h>
#include "../fw2/ui/meter_winamp_bars.c"
#include "p2_bars_pack_trace.h"
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { bp_hash_rect(x, y, w, h, c); }
static void run(const mtr_in_t *in) { meter_winamp_bars_tick(in); }
static void report(int s, uint32_t n, uint64_t h) { printf("S %d %u %016llx\n", s, n, (unsigned long long)h); }
int main(void)
{
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    bars_trace(meter_winamp_bars_mv, meter_winamp_bars_apply, run, report);
    return 0;
}
