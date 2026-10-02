/* Throttle for the heavy full-repaint meter (B-524/B-525): redraw at most every `min_cyc` cycles, and not at all while the audio FIFO is low, adding the time
 * that was skipped to the next call's dt so the history still scrolls at the right speed.
 *
 * Pure logic, host-tested (sim/test_meter_policy.py). The accumulated dt is CLAMPED to MP_MAX_DT_MS: Layered Wave multiplies dt by speed (<= 240), the
 * history length (<= 400) and 256 in 32-bit arithmetic, which overflows above about 87 ms, and lw_coef() multiplies dt by 2^24 (overflow at 256 ms). A long
 * FIFO yield (up to 2 s) must therefore drop the surplus rather than hand it on: the meter simply does not catch up after a stall. */
#ifndef TAU_METER_POLICY_H
#define TAU_METER_POLICY_H
#include <stdint.h>

#define MP_MAX_DT_MS 80u

typedef struct { uint32_t last_cyc, skip_ms; uint8_t have; } mp_t;

/* Returns 1 to draw now (*dt_out = this call's dt plus the skipped time, clamped), 0 to skip (the dt is banked). A forced repaint always draws. */
static int mp_throttle(mp_t *m, uint32_t now, uint32_t min_cyc, uint32_t dt_ms, int force, int afford, uint32_t *dt_out)
{
    if (!force && ((m->have && (uint32_t)(now - m->last_cyc) < min_cyc) || !afford)) {
        m->skip_ms += dt_ms;
        if (m->skip_ms > MP_MAX_DT_MS) m->skip_ms = MP_MAX_DT_MS;
        return 0;
    }
    uint32_t dt = dt_ms + m->skip_ms;
    if (dt > MP_MAX_DT_MS) dt = MP_MAX_DT_MS;
    m->skip_ms = 0u;
    *dt_out = dt;
    return 1;
}
/* Call after a draw that went ahead. */
static void mp_drew(mp_t *m, uint32_t now) { m->last_cyc = now; m->have = 1u; }
#endif
