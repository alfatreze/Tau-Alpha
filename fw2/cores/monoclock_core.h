/* fw2/cores/monoclock_core.h -- a monotone seconds counter from the 32-bit free-running cycle counter (spec principle P2: counters never reset, deltas are taken in one place).
 * The cycle counter wraps every 2^32 / 66,666,667 = 64.4 s; a "(now - start) / hz" computed from it is wrong after the first wrap (UPTIME read 12 after 76 s on a Pocket,
 * 2026-10-16). mc_update() must be called at least once per 64 s (the UI calls it 4 times a second); it adds each delta to a remainder and carries whole seconds. */
#ifndef MONOCLOCK_CORE_H
#define MONOCLOCK_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint32_t last, rem, sec; uint8_t started; } mc_t;
static inline uint32_t mc_update(mc_t *m, uint32_t now, uint32_t hz)
{
    if (!m->started) { m->started = 1; m->last = now; return m->sec; }
#if BUG == 1
    uint32_t d = now > m->last ? now - m->last : 0u;                 /* a wrap loses the whole delta */
#else
    uint32_t d = now - m->last;                                      /* unsigned: correct across the wrap */
#endif
    m->last = now;
#if BUG == 2
    m->rem = d;                                                      /* the remainder is dropped */
#else
    m->rem += d;
#endif
    while (m->rem >= hz) { m->rem -= hz; m->sec++; }
    return m->sec;
}
#endif
