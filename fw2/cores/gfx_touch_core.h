/* fw2/cores/gfx_touch_core.h -- the rows a run of draw commands touched (docs/features/meters/METER_FRAMEWORK_FINDINGS.md finding 7). Pure. Host-tested (sim/test_p2_gfx_touch.py).
 * Every command gfx.c issues reports the rows it writes: the VISIBLE rows (0..359: what the beam can show half old, half new) and the STASH rows (360 and below: off-screen scratch, shared by both buffers, never
 * seen: gfx_scroll_down's temporary is one). The host resets the span before a meter's tick and reads it after: the meter's own picture rows, for the beam test (helios_core.h hl_rows_safe) and the Info page. */
#ifndef GFX_TOUCH_CORE_H
#define GFX_TOUCH_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define GT_VISIBLE_ROWS 360u
typedef struct { uint16_t y0, y1, s0, s1; } gt_t;               /* visible span [y0, y1) and stash span [s0, s1); empty when y0 >= y1 (s0 >= s1) */
static inline void gt_reset(gt_t *t) { t->y0 = 0xFFFFu; t->y1 = 0; t->s0 = 0xFFFFu; t->s1 = 0; }
static inline void gt_note(gt_t *t, uint32_t y, uint32_t h)     /* a command wrote h rows from row y */
{
    if (!h) return;
    const uint32_t e = y + h;
    if (y < GT_VISIBLE_ROWS) {
#if BUG == 1
        const uint32_t ve = e;                                  /* no clip: a command that runs on into the stash counts those rows as visible */
#else
        const uint32_t ve = e < GT_VISIBLE_ROWS ? e : GT_VISIBLE_ROWS;
#endif
        if (y < t->y0) t->y0 = (uint16_t)y;
        if (ve > t->y1) t->y1 = (uint16_t)ve;
    }
    if (e > GT_VISIBLE_ROWS) {
        const uint32_t s = y > GT_VISIBLE_ROWS ? y : GT_VISIBLE_ROWS;
#if BUG == 2
        if (s < t->s0) t->s0 = (uint16_t)y;                     /* the stash start taken from the command, not clipped to the stash */
#else
        if (s < t->s0) t->s0 = (uint16_t)s;
#endif
        if (e > t->s1) t->s1 = (uint16_t)e;
    }
}
static inline int gt_visible(const gt_t *t) { return t->y0 < t->y1; }
#endif
