/* fw/headroom.h -- decode headroom, and how fast the audio could be consumed (Cymo C0a, B-538). Pure arithmetic, host-tested.
 *
 * The question the tempo feature needs answered: with this track, on this build, how much spare CPU is there, and what speed does that allow? The raw
 * material is already measured by the player: `idle` is the share of each second the decode loop spent blocked on a FULL audio FIFO, i.e. spare CPU
 * (fl_idle_pct, latched once a second). This header turns it into (a) the WORST second since the speed or track last changed, because underruns come from the
 * worst second and not the average, and (b) a projected maximum speed.
 *
 * Projection: busy = 100 - idle at the current speed s (num/den). Work per second of audio at 1.0x is busy * den / num, assuming the busy time scales with the
 * rate at which audio is consumed. That is an approximation: UI, meters and file I/O do not scale, so a real run at the projected speed can only be BETTER than the
 * projection on the decode side, but the file reads and the cover decode are not covered -- treat it as a guide, then confirm with a real run at that speed
 * (the Info UNDERRUNS row). `extra_pct` is a fixed extra load in percent of the CPU that does not scale with speed: the tempo stretcher's search cost
 * (an ESTIMATE until measured, see HR_WSOLA_EST_PCT). */
#ifndef HEADROOM_H
#define HEADROOM_H
#include <stdint.h>

#define HR_WSOLA_EST_PCT 16u   /* ESTIMATE: tools/lab/cymo_tempo_model.py counts 1.61 M MAC per output second, about 12-20% of the 66.7 MHz CPU at 5-8 cycles per MAC. Replace by a measurement. */
#define HR_SETTLE_SECS   2u    /* ignore the first seconds after a track load or speed change: loading and prefill are not steady-state decode */

typedef struct { uint8_t min_idle, secs, last, max_io; } hr_t;   /* last = idle of the latest counted second, 255 = none yet; max_io = the most time any counted second spent blocked waiting for file bytes (percent) */

static inline void hr_reset(hr_t *h) { h->min_idle = 100u; h->secs = 0u; h->last = 255u; h->max_io = 0u; }

/* Call once per latched second while playing, with the idle percentage of that second -- and only for seconds where nothing but playback was running:
 * the caller must NOT count seconds with a menu or the Info page up, because redrawing a page (every scroll step is a full redraw) is real CPU load that has nothing
 * to do with decoding (TAU_DEV_73 read MIN4 and CPU 96% while the Info page was being scrolled, against 56% idle at rest). */
static inline void hr_update(hr_t *h, uint32_t idle, uint32_t io)
{
    if (h->secs < 255u) h->secs++;
    if (h->secs <= HR_SETTLE_SECS) return;
    h->last = (uint8_t)idle;
    if (idle < h->min_idle) h->min_idle = (uint8_t)idle;
    if (io > h->max_io) h->max_io = (uint8_t)(io > 100u ? 100u : io);
}

/* Projected maximum speed, times 100 (250 = 2.5x), at `idle` percent idle while running at speed num/den, with `extra_pct` of fixed extra load.
 * Returns 0 if the extra load alone leaves nothing. Busy is floored at 1% so a fully idle reading cannot divide by zero (it then reads as a very high speed). */
static inline uint32_t hr_max_speed_x100(uint32_t idle, uint32_t num, uint32_t den, uint32_t extra_pct)
{
    if (idle > 99u) idle = 99u;
    if (extra_pct >= 100u) return 0u;
    const uint32_t busy = 100u - idle;                      /* 1..100 */
    return ((100u - extra_pct) * 100u * num) / (busy * den);
}

/* ---- Every underrun, not just the first (B-546) ----------------------------------------------------------------------------------------------------------
 * The hardware `underrun` flag is sticky until the next flush and the firmware's shadow of it (under_shadow) is cleared only by a flush, so the Info
 * UNDERRUNS row counts at most ONE event per track start, seek or resume: the start-of-track transient normally uses it up, and an audible underrun later in
 * the same stretch is invisible. The FIFO's own EMPTY bit is not sticky, and every push already reads the status word, so a stall shows as "the FIFO is empty
 * at a push, after it had been seen full since the last flush" (empty before that is just the prefill). One count per stall, however many pushes it spans. */
typedef struct { uint32_t n; uint8_t full_seen, empty_prev; } ur_t;
static inline void ur_flush(ur_t *u) { u->full_seen = 0u; u->empty_prev = 0u; }
static inline void ur_note(ur_t *u, uint32_t full, uint32_t empty)
{
    if (full) { u->full_seen = 1u; u->empty_prev = 0u; }
    else if (empty) { if (u->full_seen && !u->empty_prev) u->n++; u->empty_prev = 1u; }
    else u->empty_prev = 0u;
}
#endif
