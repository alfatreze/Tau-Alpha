/* fw2/cores/meter_cost_core.h -- what each meter's tick costs, measured per meter (Talos 2 T4 gate: choose the next graphics primitive from data, D-T05).
 * Pure and host-tested (sim/test_p2_meter_cost.py). A tick contributes: the engine commands it issued, the CPU cycles it took, and how many of those cycles were spent waiting for a free
 * command-queue slot (stall) and in fences. The averages are moving averages (new = old + (x - old) / 8, the first tick sets them); cmd_avg16 is the command average times 16 so a meter with
 * 3 commands a tick and one with 3.4 differ. Commands above 4,095 saturate. Read with the Report (rows "M CMD MAX", "M CMD AVG16", "M CYC AVG", "M STALL AVG", "M FENCE AVG", one per meter). */
#ifndef METER_COST_CORE_H
#define METER_COST_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint16_t cmd_max, cmd_avg16, n; uint32_t cyc_avg, stall_avg, fence_avg; } mcost_t;
static inline uint32_t mcost_ema(uint32_t old, uint32_t x, uint32_t first)
{
    if (first) return x;
    return (uint32_t)((int32_t)old + (((int32_t)x - (int32_t)old) / (BUG == 1 ? 1 : 8)));       /* BUG 1 (mutant): no smoothing at all */
}
static inline void mcost_add(mcost_t *c, uint32_t cmds, uint32_t cyc, uint32_t stall, uint32_t fence)
{
    if (cmds > 4095u) cmds = 4095u;
    const uint32_t first = c->n == 0u;
    if (BUG != 2 && cmds > c->cmd_max) c->cmd_max = (uint16_t)cmds;                              /* BUG 2 (mutant): the maximum never rises */
    c->cmd_avg16 = (uint16_t)mcost_ema(c->cmd_avg16, cmds << 4, first);
    c->cyc_avg   = mcost_ema(c->cyc_avg, cyc, first);
    c->stall_avg = mcost_ema(c->stall_avg, BUG == 3 ? 0u : stall, first);                        /* BUG 3 (mutant): the stall is not recorded */
    c->fence_avg = mcost_ema(c->fence_avg, fence, first);
    if (c->n < 0xFFFFu) c->n++;
}
#endif
