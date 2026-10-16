/* fw2/cores/blank_core.h -- Player 2 screen blank (the old player's feature): after N minutes with no button press the screen goes black; playback is untouched, only drawing stops; the first press wakes
 * the screen and is swallowed (reaching for a sleeping player must not pause it or skip a track). The Pocket's backlight cannot be reached from a core, so this is a dark screen, not a power saving.
 * Time is kept in whole seconds from a one-second cycle-counter deadline: a minutes-times-clock multiply would overflow the 32-bit cycle counter (it wraps every ~64 s at 66.7 MHz). Pure, host-tested
 * (sim/test_p2_blank.py). */
#ifndef BLANK_CORE_H
#define BLANK_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#define BL_OPTS 5u
static const uint8_t bl_minutes[BL_OPTS] = { 0u, 1u, 5u, 10u, 30u };       /* the choice list: Off, 1, 5, 10, 30 minutes */
typedef struct { uint32_t sel, sec, deadline; uint8_t blanked; } bl_t;
enum { BL_NONE = 0, BL_SLEEP, BL_WAKE };
static inline void bl_touch(bl_t *b, uint32_t now, uint32_t hz) { b->sec = 0; b->deadline = now + hz; }
/* Once per loop pass with the key edges of this pass. Returns BL_SLEEP when the screen should go black now, BL_WAKE when this pass's press woke it (the edges must then be swallowed by the caller),
 * else BL_NONE. */
static inline int bl_step(bl_t *b, uint32_t now, uint32_t hz, uint32_t edge_or_release)
{
    if (edge_or_release) {
        bl_touch(b, now, hz);
        if (b->blanked) { b->blanked = 0; return BL_WAKE; }
    }
    if (b->blanked || b->sel == 0u || b->sel >= BL_OPTS) return BL_NONE;
    if ((int32_t)(now - b->deadline) < 0) return BL_NONE;
    b->deadline = now + hz;
#if BUG == 1
    b->sec += 60u;                                                    /* mutant: a second counts as a minute */
#else
    if (b->sec < 0xFFFFu) b->sec++;
#endif
    if (b->sec >= (uint32_t)bl_minutes[b->sel] * 60u) { b->blanked = 1; return BL_SLEEP; }
    return BL_NONE;
}
#endif
