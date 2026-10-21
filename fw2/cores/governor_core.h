/* fw2/cores/governor_core.h -- Player 2 governor (docs/features/PLAYER_V2_SPEC.md section 9.2). Pure logic, host-tested (sim/test_p2_governor.py).
 *
 * The decoder is a producer with a fill target; everything else the loop does is paid for out of the CPU time the decoder leaves. The ring level is the only input:
 * as the lead shrinks the governor sheds work in a fixed order, and gives it back only when the lead has recovered by a margin (hysteresis), so the mode does not flap.
 *
 *   NORMAL      lead above 50% of the usable ring    full UI, full meters
 *   SHED_UI     25% to 50%                           UI slices halved, animations and software meters off
 *   AUDIO_ONLY  10% to 25%                           decode only (input is still read)
 *   PANIC       below 10%                            decode only, input ignored except STOP
 *
 * Decode stops at HI (95%): a full ring is the steady state at 1.00x and hands the CPU to the UI. */
#ifndef GOVERNOR_CORE_H
#define GOVERNOR_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif

typedef enum { GOV_NORMAL = 0, GOV_SHED_UI = 1, GOV_AUDIO_ONLY = 2, GOV_PANIC = 3 } gov_mode_t;

typedef struct {
    uint32_t cap;                /* usable pairs of the ring */
    uint32_t t50, t25, t10, hi;  /* thresholds in pairs */
    uint32_t hyst;               /* pairs a mode needs above its threshold to be left upward (5% of cap) */
    gov_mode_t mode;
    uint32_t entered[4];         /* times each mode was entered */
} gov_t;

static inline void gov_init(gov_t *g, uint32_t usable_pairs)
{
    g->cap = usable_pairs;
    g->t50 = usable_pairs / 2u;
    g->t25 = usable_pairs / 4u;
    g->t10 = usable_pairs / 10u;
    g->hi  = usable_pairs - usable_pairs / 20u;
    g->hyst = usable_pairs / 20u;
    g->mode = GOV_NORMAL;
    for (int i = 0; i < 4; i++) g->entered[i] = 0;
    g->entered[GOV_NORMAL] = 1;
}

/* The mode the level asks for, with no memory. */
static inline gov_mode_t gov_raw(const gov_t *g, uint32_t fill)
{
#if BUG == 1
    if (fill > g->t50) return GOV_NORMAL;              /* strict: a level exactly at the threshold falls a mode */
#else
    if (fill >= g->t50) return GOV_NORMAL;
#endif
    if (fill >= g->t25) return GOV_SHED_UI;
    if (fill >= g->t10) return GOV_AUDIO_ONLY;
    return GOV_PANIC;
}

/* Down at once, up only when the level clears the threshold of the mode above by the hysteresis margin. */
static inline gov_mode_t gov_update(gov_t *g, uint32_t fill)
{
    gov_mode_t want = gov_raw(g, fill);
    if (want > g->mode) { g->mode = want; g->entered[want]++; return g->mode; }          /* worse: immediately */
    if (want < g->mode) {
#if BUG == 2
        uint32_t margin = 0;                                                               /* no hysteresis: the mode flaps around a threshold */
#else
        uint32_t margin = g->hyst;
#endif
        uint32_t thr = (g->mode == GOV_PANIC) ? g->t10 : (g->mode == GOV_AUDIO_ONLY) ? g->t25 : g->t50;
        if (fill >= thr + margin) {
            gov_mode_t up = (gov_mode_t)(g->mode - 1);                                    /* one step per call: recovery is gradual */
#if BUG == 3
            up = want;                                                                    /* jumps straight to the wanted mode */
#endif
            g->mode = up; g->entered[up]++;
        }
    }
    return g->mode;
}

/* Meter degrade level 0..3: how much the meters must give up so the audio keeps its lead (fw2/ui/meter.h mtr_in_t.degrade; meters.c skips windows by it for every meter). Inside NORMAL the ring level above the
 * 50% line is split in thirds of the span up to the decode ceiling: top third 0 (full), middle third 1 (every second picture), lower third 2 (every fourth); SHED_UI and worse 3 (the meters are off, drawn again
 * when the mode recovers). No memory: a level that flaps costs only a changed picture rate, never a decision about audio. */
static inline uint32_t gov_degrade(const gov_t *g, gov_mode_t mode, uint32_t fill)
{
    if (mode != GOV_NORMAL) return 3u;
    const uint32_t span = g->hi - g->t50, above = fill > g->t50 ? fill - g->t50 : 0u;
#if BUG == 4
    return above >= span / 2u ? 0u : above >= span / 4u ? 1u : 2u;                       /* wrong thirds */
#else
    return above >= span * 2u / 3u ? 0u : above >= span / 3u ? 1u : 2u;
#endif
}

/* What the rest of the player sees (mode and meter degrade level). While nothing is decoding (idle, stopped, between tracks) the audio is not at risk, so both read 0 at once instead of keeping the last value
 * the decoder left (a stale level made the meters draw at half or quarter rate, or not at all, with nothing playing); the governor's own state keeps its memory and carries on from it when decoding resumes. */
static inline void gov_publish(gov_t *g, int decoding, uint32_t fill, uint32_t *mode, uint32_t *degrade)
{
    if (!decoding) {
#if BUG != 5
        *mode = 0u; *degrade = 0u;
#endif
        return;
    }
    *mode = (uint32_t)gov_update(g, fill); *degrade = gov_degrade(g, g->mode, fill);
}

/* True while the decoder should keep producing. */
static inline int gov_decode_wanted(const gov_t *g, uint32_t fill) { return fill < g->hi; }
#endif
