/* fw2/cores/pack_strike_core.h -- per-pack cycle limits (decision D-M20, B-811). Pure: no registers. Host-tested (sim/test_p2_pack_strike.py).
 *
 * A pack is code from the card that runs with full access to the hardware, and the host cannot interrupt a tick that is already running (no watchdog in the bitstream): what it CAN do is notice afterwards. A drawn tick of a
 * pack that took more cycles than its limit (manifest p2.max_cycles; a forced repaint, the costliest picture by design, is not judged) is a strike; after PS_MAX_STRIKES the pack is disabled for the session (status
 * MPK_E_STRIKE, Info PACK ERR 40) and the meter falls back to its built-in code (or draws nothing when it has none). One strike is forgiven for every PS_FORGIVE_TICKS drawn ticks inside the limit, so a single stall (a card
 * read in the middle of a tick) cannot slowly disable a good pack over a long session. A pure infinite loop is cut off by the hardware watchdog (tau_wdog.sv, on by default, TAU_NO_WDOG leaves it out; wd_run in platform.c, plan
 * docs/features/PACK_WATCHDOG_PLAN.md): the window is wd_limit() of the soft limit, and a cut-off pack is struck out at once (the call site in meters.c sets MPK_E_STRIKE). On a bitstream without the watchdog an endless loop still hangs the player. */
#ifndef PACK_STRIKE_CORE_H
#define PACK_STRIKE_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define PS_MAX_STRIKES 3u
#define PS_FORGIVE_TICKS 64u
typedef struct { uint8_t strikes, clean; } ps_t;
/* The hardware watchdog's window for one pack tick, in clk cycles: WD_MULT times the pack's soft limit, never below WD_FLOOR (a pack that is a little slow is the soft limit's business, not the watchdog's) and never above WD_CEIL
 * (the audio ring holds about a second: a longer cut-off would already have underrun). A soft limit of 0 (none) gets WD_CEIL. Values per the plan section 7, awaiting the owner's confirmation. */
#define WD_MULT  4u
#define WD_FLOOR 2000000u
#define WD_CEIL  20000000u
static inline uint32_t wd_limit(uint32_t soft)
{
    if (!soft) return WD_CEIL;
#if BUG == 4
    uint32_t v = soft * WD_MULT;                                           /* no overflow guard: a huge soft limit wraps to a tiny window */
#else
    uint32_t v = soft > WD_CEIL / WD_MULT ? WD_CEIL : soft * WD_MULT;
#endif
    return v < WD_FLOOR ? WD_FLOOR : v;
}
static inline void ps_reset(ps_t *p) { p->strikes = 0; p->clean = 0; }
/* Judge one drawn tick; returns 1 when the pack has now struck out. limit 0 = no limit. */
static inline int ps_judge(ps_t *p, uint32_t spent, uint32_t limit)
{
    if (!limit) return 0;
#if BUG == 1
    if (spent >= limit)                                                    /* off by one: exactly at the limit is a strike */
#else
    if (spent > limit)
#endif
    {
        p->clean = 0;
        if (p->strikes < 255u) p->strikes++;
#if BUG == 2
        return p->strikes > PS_MAX_STRIKES;                                 /* one strike too many allowed */
#else
        return p->strikes >= PS_MAX_STRIKES;
#endif
    }
#if BUG != 3
    if (p->strikes && ++p->clean >= PS_FORGIVE_TICKS) { p->strikes--; p->clean = 0; }
#endif
    return 0;
}
#endif
