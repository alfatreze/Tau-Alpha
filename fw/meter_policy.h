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

typedef struct { uint32_t last_cyc, skip_ms, cost_cyc; uint8_t have; } mp_t;   /* cost_cyc: what the last draw cost, for the duty cap */

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

/* The mandatory throttle for EVERY meter (framework rule, fw/meter.h): a meter may spend at most `duty_pct` percent of the CPU. After a draw that cost C cycles
 * the next one is not allowed until C*100/duty_pct cycles have passed since the last started (so a 10 ms draw at 40% repeats at most every 25 ms), on top of the
 * meter's own minimum interval and the audio-FIFO gate. A cheap meter's interval is far below the frame time, so it is never held back; an expensive one is
 * stretched exactly as much as it would otherwise have taken from the decoder. duty_pct 0 = no duty cap. The first draw (no cost known yet) is never held. */
static int mp_gate(mp_t *m, uint32_t now, uint32_t min_cyc, uint32_t duty_pct, uint32_t dt_ms, int force, int afford, uint32_t *dt_out)
{
    uint32_t need = min_cyc;
    if (duty_pct && m->cost_cyc) {
        const uint32_t d = (uint32_t)(((uint64_t)m->cost_cyc * 100u) / duty_pct);
        if (d > need) need = d;
    }
    return mp_throttle(m, now, need, dt_ms, force, afford, dt_out);
}
/* Call after a gated draw with its measured cost in cycles. */
static void mp_drew_cost(mp_t *m, uint32_t now, uint32_t cost_cyc) { mp_drew(m, now); m->cost_cyc = cost_cyc; }
#endif
