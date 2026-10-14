/* fw/pcm_ring.h -- the decoded-PCM ring of gapless playback, step G1 (docs/features/GAPLESS_SPEC.md section 4; B-730).
 *
 * The decoders no longer write into the 2,048-pair hardware FIFO (46 ms): they write RAW pairs (before volume and fade) into a ring, in the firmware a region of the PSRAM window,
 * and a drain moves pairs from the ring into the FIFO whenever the FIFO has room, applying volume and the discontinuity fade at that point. Gain at the drain, not at the put, so a
 * volume change is heard at once instead of after the ring's depth, and a flush that arms the fade-in fades the first pairs HEARD, not the first decoded. The stream reaching the
 * FIFO is, sample for sample, what pcm_push_pairs() would have written (sim/test_pcm_ring.py proves it through random put/drain/flush sequences).
 * The includer supplies the hardware (as pcm_push.h does): rd_st() the FIFO status word, wr(w) the audio write, spin() called while the producer waits for room (it must drain the ring;
 * nonzero return aborts the put). All indices are in pairs; cap need not be a power of two. */
#ifndef PCM_RING_H
#define PCM_RING_H
#include "pcm_push.h"

typedef struct { volatile uint32_t *buf; uint32_t cap, w, r, n; } pcm_ring_t;   /* w, r in [0, cap); n = pairs held */

static inline void     pcm_ring_init(pcm_ring_t *q, volatile uint32_t *buf, uint32_t cap) { q->buf = buf; q->cap = cap; q->w = q->r = q->n = 0u; }
static inline void     pcm_ring_flush(pcm_ring_t *q) { q->w = q->r = q->n = 0u; }
static inline uint32_t pcm_ring_fill(const pcm_ring_t *q) { return q->n; }
static inline uint32_t pcm_ring_room(const pcm_ring_t *q) { return q->cap - q->n; }

/* Producer: put n_pairs raw pairs (mono duplicates). Waits (spin) while the ring is full. Returns 1, or 0 if spin() aborted (pairs before the abort point stay in the ring). */
static inline uint8_t pcm_ring_put(pcm_ring_t *q, const int16_t *pcm, uint32_t n_pairs, uint32_t stereo, int (*spin)(void))
{
    uint32_t done = 0;
    while (done < n_pairs) {
        while (q->n >= q->cap) { if (spin()) return 0u; }
        uint32_t k = q->cap - q->n; if (k > n_pairs - done) k = n_pairs - done;
        for (uint32_t j = 0; j < k; j++, done++) {
            const int32_t l = pcm[stereo ? 2u * done : done];
            const int32_t r = stereo ? pcm[2u * done + 1u] : l;
            q->buf[q->w] = pcm_pack(l, r);
            if (++q->w == q->cap) q->w = 0u;
        }
        q->n += k;
    }
    return 1u;
}

/* Consumer: one status read, then move min(held, room) pairs through volume and fade into the FIFO. Returns the pairs moved. */
static inline uint32_t pcm_ring_drain(pcm_ring_t *q, uint32_t (*rd_st)(void), void (*wr)(uint32_t), pcm_vol_t *vol, uint32_t *fade_left, uint32_t fade_samples)
{
    if (!q->n) return 0u;
    const uint32_t room = PCM_FIFO_DEPTH - PCM_ST_LEVEL(rd_st());
    uint32_t k = room < q->n ? room : q->n;
    const uint32_t moved = k;
    while (k--) {
        const uint32_t w = q->buf[q->r];
        int32_t l = (int16_t)(w & 0xFFFFu), r = (int16_t)(w >> 16);
        pcm_gain_apply(&l, &r, vol, fade_left, fade_samples);
        wr(pcm_pack(l, r));
        if (++q->r == q->cap) q->r = 0u;
    }
    q->n -= moved;
    return moved;
}
#endif
