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
static inline uint32_t prh_fill(const prh_t *r, uint32_t ridx) { return r->w >= ridx ? r->w - ridx : r->w + r->size - ridx; }
static inline uint32_t prh_room(const prh_t *r, uint32_t ridx) { const uint32_t f = prh_fill(r, ridx); return f + r->margin + 1u >= r->size ? 0u : r->size - 1u - r->margin - f; }

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
#endif
