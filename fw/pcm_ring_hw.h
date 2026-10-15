/* fw/pcm_ring_hw.h -- the CPU side of the hardware PCM ring reader (gapless, B-746; hardware: src/fpga/core/tau_pcm_ring.sv, design: docs/features/PCM_RING_HW_DESIGN.md).
 *
 * The CPU stores decoded pairs ({right << 16 | left}, RAW: the gain stage sits at the FIFO output) into a ring in the PSRAM through the uncached window and publishes a WRITE INDEX
 * (RING_WPTR). The reader (hardware) only sees pairs up to the published index, so the data word must be stored before the index that exposes it. The ring never fills completely
 * (capacity size - 1; empty is write index == read index). All indices are in pairs, 0 <= i < size. Pure, host-tested (sim/test_pcm_ring_hw.py).
 * Hooks: rd_ridx() reads RING_RIDX, wr_word(i, w) stores one word at ring slot i, wr_wptr(i) publishes the index, spin() runs while the ring is full (service input and I/O; nonzero aborts). */
#ifndef PCM_RING_HW_H
#define PCM_RING_HW_H
#include <stdint.h>

typedef struct {
    uint32_t (*rd_ridx)(void);
    void     (*wr_word)(uint32_t idx, uint32_t word);
    void     (*wr_wptr)(uint32_t idx);
    int      (*spin)(void);
} prh_hooks_t;
typedef struct { uint32_t size, w, margin; } prh_t;      /* w = the write index (what RING_WPTR holds or is about to); margin = slots kept untouched behind the read index (>= the FIFO depth plus the meters' read-back window, so every pair still in the FIFO or just heard stays readable) */

static inline void prh_init(prh_t *r, uint32_t size, uint32_t margin, uint32_t w) { r->size = size; r->margin = margin; r->w = w; }
static inline __attribute__((always_inline)) uint32_t prh_fill(const prh_t *r, uint32_t ridx) { return r->w >= ridx ? r->w - ridx : r->w + r->size - ridx; }
static inline __attribute__((always_inline)) uint32_t prh_room(const prh_t *r, uint32_t ridx) { const uint32_t f = prh_fill(r, ridx); return f + r->margin + 1u >= r->size ? 0u : r->size - 1u - r->margin - f; }

#define PRH_PUBLISH 64u      /* pairs between index publications while a long block is put (the reader can start on the first 64 while the rest is still being written) */

/* Put n pairs (mono duplicated). Returns 1, or 0 if spin() aborted while the ring was full (the pairs stored before that point are published and stay in the ring). */
static inline uint8_t prh_put(prh_t *r, const prh_hooks_t *h, const int16_t *pcm, uint32_t n, uint32_t stereo)
{
    uint32_t done = 0, pending = 0, room = 0;
    while (done < n) {
        if (!room) {
            for (;;) {
                room = prh_room(r, h->rd_ridx());
                if (room) break;
                if (pending) { h->wr_wptr(r->w); pending = 0; }     /* publish what is stored before waiting: the reader may be starved by our own unpublished pairs */
                if (h->spin()) return 0u;
            }
        }
        const int32_t l = pcm[stereo ? 2u * done : done];
        const int32_t rr = stereo ? pcm[2u * done + 1u] : l;
        h->wr_word(r->w, ((uint32_t)(uint16_t)(int16_t)rr << 16) | (uint32_t)(uint16_t)(int16_t)l);
        if (++r->w == r->size) r->w = 0u;
        room--; done++; pending++;
        if (pending >= PRH_PUBLISH) { h->wr_wptr(r->w); pending = 0; }
    }
    if (pending) h->wr_wptr(r->w);
    return 1u;
}

/* One pair (the FLAC/tempo path). `publish` = publish the index now. */
static inline uint8_t prh_put1(prh_t *r, const prh_hooks_t *h, int32_t l, int32_t rr, uint32_t publish)
{
    while (prh_room(r, h->rd_ridx()) == 0u) { h->wr_wptr(r->w); if (h->spin()) return 0u; }
    h->wr_word(r->w, ((uint32_t)(uint16_t)(int16_t)rr << 16) | (uint32_t)(uint16_t)(int16_t)l);
    if (++r->w == r->size) r->w = 0u;
    if (publish) h->wr_wptr(r->w);
    return 1u;
}

/* Pairs decoded but not yet heard: the unread ring plus what the FIFO still holds. The clock and progress bar subtract this from the decoded position. */
static inline uint32_t prh_lead(const prh_t *r, uint32_t ridx, uint32_t fifo_level) { return prh_fill(r, ridx) + fifo_level; }

/* ---- the heard position (clock, meters) ----------------------------------------------------------------------------------------------------------------------------
 * The reader pushes pair number p (0, 1, 2, ... = RING_CNT) from ring slot ridx_at_that_time, and the FIFO holds the last `level` of them, so the pairs HEARD so far are
 * cnt - level. A meter feed takes the pairs heard since `fed` (at most max_n of them: older ones are skipped), whose slots lie `cnt - first` behind the read index. The caller must
 * set fed = cnt when the ring is flushed (the dropped pairs are never pushed, so cnt does not move, but ridx jumps and the relation to `fed` is gone). */
static inline uint32_t prh_meter_span(uint32_t cnt, uint32_t level, uint32_t fed, uint32_t ridx, uint32_t size, uint32_t max_n, uint32_t *pos, uint32_t *new_fed)
{
    const uint32_t heard = cnt - level;
    int32_t n = (int32_t)(heard - fed);
    if (n <= 0) { *new_fed = fed; *pos = 0u; return 0u; }
    if ((uint32_t)n > max_n) { fed = heard - max_n; n = (int32_t)max_n; }
    const uint32_t back = cnt - fed;                        /* slots behind ridx of the first pair to feed */
    *pos = ridx >= back ? ridx - back : ridx + size - back;
    *new_fed = fed + (uint32_t)n;
    return (uint32_t)n;
}
/* The decoded position (in pairs) minus the lead, never below zero: what the clock shows. */
static inline uint32_t prh_heard_pos(uint32_t decoded_pairs, uint32_t lead) { return decoded_pairs > lead ? decoded_pairs - lead : 0u; }
/* The natural-end wait: what to do now. 0 = keep waiting, 1 = everything has played out, 2 = give up (a reload request, or too long without a pause). */
static inline uint32_t prh_playout_step(uint32_t reload_pending, uint32_t paused, uint32_t ring_empty, uint32_t fifo_empty, uint32_t elapsed_cyc, uint32_t limit_cyc)
{
    if (reload_pending) return 2u;
    if (paused) return 0u;                                  /* held: wait for the resume, the clock does not run */
    if (ring_empty && fifo_empty) return 1u;
    if (elapsed_cyc > limit_cyc) return 2u;
    return 0u;
}

/* ---- the gapless boundary (G2b, docs/features/GAPLESS_SPEC.md section 11) ---------------------------------------------------------------------------------------------
 * `put_total` counts the pairs the CPU has put (the firmware sets it to RING_CNT at every flush: the unread pairs are dropped, the FIFO emptied, and what was put and dropped is not
 * pushed). At the end of a track the marker is the put_total at that moment: every pair of the old track is in the ring, none of the next. The old track is HEARD to its end when the pairs
 * released by the FIFO (RING_CNT - level) reach the marker (a signed compare: the counters wrap). While it is pending the old track's clock is its frozen decoded position minus what is
 * still unheard. */
static inline int prh_marker_reached(uint32_t heard, uint32_t marker) { return (int32_t)(heard - marker) >= 0; }
static inline uint32_t prh_old_clock_pairs(uint32_t pre_pp, uint32_t marker, uint32_t heard)
{
    const int32_t d = (int32_t)(marker - heard);
    const uint32_t unheard = d > 0 ? (uint32_t)d : 0u;
    return pre_pp > unheard ? pre_pp - unheard : 0u;
}
#endif
