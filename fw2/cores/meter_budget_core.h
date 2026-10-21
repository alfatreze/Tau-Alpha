/* fw2/cores/meter_budget_core.h -- the per-meter draw-command budget (docs/features/meters/METER_FRAMEWORK_FINDINGS.md finding 7). Pure: no registers. Host-tested (sim/test_p2_meter_budget_cmds.py).
 *
 * Every draw command a meter issues (built-in or pack: both reach the engine through gfx.c, which counts them in gfx_cmds) is counted around its tick. A tick that issued more commands than the meter's
 * ceiling (manifest p2.cmds) raises the meter's OWN degrade level (0..2): the host then draws only every 2nd / 4th window, exactly as the governor's degrade does, and tells the meter (mtr_in_t.degrade) so it can thin
 * its detail too. The effective level is the larger of the two. After MB_RECOVER consecutive drawn ticks inside the budget the level steps back down. A forced repaint (the whole box, by design the costliest picture) is not judged.
 * A budget of 0 means unbudgeted. The budget is a guarantee that a heavy meter cannot keep the audio loop busy more than every second or fourth window; it cannot interrupt one tick already running. */
#ifndef MTR_BUDGET_CORE_H
#define MTR_BUDGET_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define MB_MAX_LEVEL 2u
#define MB_RECOVER 16u
typedef struct { uint8_t level, clean; } mb_t;
static inline void mb_reset(mb_t *m) { m->level = 0; m->clean = 0; }
#if BUG == 1
static inline int mb_over(uint32_t used, uint32_t limit) { return limit && used >= limit; }       /* off by one: a tick exactly at the ceiling counts as over */
#else
static inline int mb_over(uint32_t used, uint32_t limit) { return limit && used > limit; }
#endif
/* Judge one drawn tick (used commands against the ceiling); returns the meter's own degrade level afterwards. */
static inline uint32_t mb_judge(mb_t *m, uint32_t used, uint32_t limit)
{
    if (!limit) return m->level;
    if (mb_over(used, limit)) {
        m->clean = 0;
        if (m->level < MB_MAX_LEVEL) m->level++;
    } else if (m->level) {
#if BUG == 2
        m->level--; m->clean = 0;                                          /* no hysteresis: one good tick drops the level */
#else
        if (++m->clean >= MB_RECOVER) { m->level--; m->clean = 0; }
#endif
    }
    return m->level;
}
static inline uint32_t mb_effective(uint32_t governor, uint32_t own) { return governor > own ? governor : own; }
#endif
