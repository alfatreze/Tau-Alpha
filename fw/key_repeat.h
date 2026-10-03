/* fw/key_repeat.h -- the one hold-to-repeat primitive for d-pad keys (B-534).
 *
 * Before this, "a step on the press, a hold delay, then steady repeats" was written out three separate times (the Settings menus, the media-library lists,
 * and nothing at all for volume, which needed one tap per step). One state word per key group, one function: it turns a held key into extra edges, so every
 * caller keeps its own edge handling unchanged -- a repeat is just another edge. Time is the free-running cycle counter; the comparison is signed so a counter
 * wrap is harmless. Nothing fires before `hold_cy` has passed since the press, and nothing fires once the keys are released. */
#ifndef KEY_REPEAT_H
#define KEY_REPEAT_H
#include <stdint.h>

typedef struct { uint32_t at; } kr_t;

/* Returns `edge` with the held `mask` keys added when a repeat is due. *rep (optional) is set to 1 on a repeat call, so callers can still tell a repeat from a
 * fresh press (e.g. a confirm key that must not auto-repeat). */
static inline uint32_t kr_step(kr_t *k, uint32_t edge, uint32_t keys, uint32_t mask, uint32_t now, uint32_t hold_cy, uint32_t period_cy, uint32_t *rep)
{
    uint32_t r = 0u;
    if (edge & mask) k->at = now + hold_cy;
    if ((keys & mask) && (int32_t)(now - k->at) >= 0) {
        k->at = now + period_cy;
        edge |= keys & mask;
        r = 1u;
    }
    if (rep) *rep = r;
    return edge;
}
#endif
