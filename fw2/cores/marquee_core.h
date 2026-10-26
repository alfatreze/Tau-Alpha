/* fw2/cores/marquee_core.h -- the Now Playing text scroll (issue 020: a long title was cut off; Player 2 had no marquee). A string wider than its box shows from its first character, waits, then drops one
 * character from the left every step until the end of it fits the box, holds there, and starts over. Pure: the caller measures the text (the font is proportional) and gives the time in milliseconds.
 * Host-tested by sim/test_p2_marquee.py. */
#ifndef MARQUEE_CORE_H
#define MARQUEE_CORE_H
#include <stdint.h>
#ifndef MQ_FIRST_MS
#define MQ_FIRST_MS 1800u               /* the string at rest before it starts to move */
#endif
#ifndef MQ_STEP_MS
#define MQ_STEP_MS 120u                 /* one character every step */
#endif
#ifndef MQ_END_MS
#define MQ_END_MS 1800u                 /* the end of the string at rest before it starts over */
#endif
typedef struct { uint32_t sig, t0; uint16_t k; } mq_t;     /* sig: which string this state belongs to (0 = none); t0: when the current position was reached; k: characters dropped from the left */
static inline uint32_t mq_sig(const char *s) { uint32_t h = 5381u; while (*s) h = h * 33u + (uint8_t)*s++; return h | 1u; }
static inline void mq_reset(mq_t *m) { m->sig = 0; m->k = 0; m->t0 = 0; }
/* The text fits its box: nothing scrolls. Otherwise: `sig` identifies the string (a new string starts again at its first character), `now` is the time in ms, `tail_fits` says whether the string from
 * the CURRENT position k fits the box. Returns k, the characters to skip. */
static inline uint32_t mq_update(mq_t *m, uint32_t sig, uint32_t now, int tail_fits)
{
    if (m->sig != sig) { m->sig = sig; m->k = 0; m->t0 = now; }
    const uint32_t dt = now - m->t0;
#if BUG == 1
    if (!tail_fits && dt >= MQ_STEP_MS) { m->k++; m->t0 = now; }                  /* no pause before it starts */
#else
    if (!tail_fits && dt >= (m->k ? MQ_STEP_MS : MQ_FIRST_MS)) { m->k++; m->t0 = now; }
#endif
#if BUG == 2
    else if (tail_fits && dt >= MQ_END_MS) { m->t0 = now; }                       /* never starts over */
#else
    else if (tail_fits && dt >= MQ_END_MS) { m->k = 0; m->t0 = now; }
#endif
    return m->k;
}
#endif
