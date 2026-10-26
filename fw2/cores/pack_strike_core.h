/* fw2/cores/pack_strike_core.h -- per-pack cycle limits (decision D-M20, B-811). Pure: no registers. Host-tested (sim/test_p2_pack_strike.py).
 *
 * A pack is code from the card that runs with full access to the hardware, and the host cannot interrupt a tick that is already running (no watchdog in the bitstream): what it CAN do is notice afterwards. A drawn tick of a
 * pack that took more cycles than its limit (manifest p2.max_cycles; a forced repaint, the costliest picture by design, is not judged) is a strike; after PS_MAX_STRIKES the pack is disabled for the session (status
 * MPK_E_STRIKE, Info PACK ERR 40) and the meter falls back to its built-in code (or draws nothing when it has none). One strike is forgiven for every PS_FORGIVE_TICKS drawn ticks inside the limit, so a single stall (a card
 * read in the middle of a tick) cannot slowly disable a good pack over a long session. A pure infinite loop still hangs the player: that needs a hardware watchdog (RTL change and a fit). */
#ifndef PACK_STRIKE_CORE_H
#define PACK_STRIKE_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define PS_MAX_STRIKES 3u
#define PS_FORGIVE_TICKS 64u
typedef struct { uint8_t strikes, clean; } ps_t;
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
