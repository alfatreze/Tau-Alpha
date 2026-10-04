/* fw/pcm_push.h -- the pure part of the one shared PCM push path (Cymo C1, B-533).
 *
 * MP3 (the decode loop) and FLAC (flac_emit) each carried their own copy of: volume, then the discontinuity fade-in, then pack and write. They had already drifted
 * apart once (FLAC shipped with no volume at all). This header holds the arithmetic exactly as it was in both copies, so it can be host-tested against the old
 * formula; the FIFO wait and the MMIO write stay in player.c's cymo_push(). Volume first, then the fade, so a fade-in at low volume stays at low volume. Capped
 * at unity: it only ever attenuates, so no clamp is needed. */
#ifndef PCM_PUSH_H
#define PCM_PUSH_H
#include <stdint.h>

static inline void pcm_gain_apply(int32_t *l, int32_t *r, int32_t vol_gain, uint32_t *fade_left, uint32_t fade_samples)
{
    if (vol_gain != 256) {
        *l = (*l * vol_gain) >> 8;
        *r = (*r * vol_gain) >> 8;
    }
    if (*fade_left) {
        int32_t g = (int32_t)((fade_samples - *fade_left) >> 3);
        *l = (*l * g) >> 8;
        *r = (*r * g) >> 8;
        (*fade_left)--;
    }
}

static inline uint32_t pcm_pack(int32_t l, int32_t r)
{
    return ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
}

/* ---- Burst push for the MP3 loop (Cymo C0 follow-up, B-592) ----------------------------------------------------------------------------------------------
 * The per-pair path reads the FIFO status register for EVERY pair (an MMIO access of tens of cycles) and waits whenever the FIFO has no room for that one pair, so in the
 * steady state (decoder ahead of the DAC, FIFO nearly full) each pair costs a status read, the gain maths and the write: measured about 166 cycles a pair, 11% of the CPU
 * (B-591). Here the status is read once per burst: wait until there is room for `need` pairs (the whole remaining frame, or `burst` pairs if more), then push as many pairs
 * as the room allows with no status read at all. The FIFO only drains while we push, so a room computed from one status read can only be an underestimate (it never drops a
 * pair); the pcm_fifo.v level register is exact in the same clock domain. The stream pushed is identical to the per-pair path, sample for sample. `h` supplies the hardware:
 * rd_st() the status word, wr(w) the audio write, wait_begin()/wait_end() bracket a blocked wait (idle accounting), spin() services input/refill and returns nonzero to abort
 * (a pending reload), note(no_room, empty) feeds the diagnostic stall counter once per status read. Returns 1, or 0 if aborted (pairs before the abort point were pushed). */
typedef struct {
    uint32_t (*rd_st)(void);
    void     (*wr)(uint32_t w);
    void     (*wait_begin)(void);
    void     (*wait_end)(void);
    int      (*spin)(void);
    void     (*note)(uint32_t no_room, uint32_t empty);
} pcm_hooks_t;

#define PCM_FIFO_DEPTH 2048u          /* pcm_fifo.v AW = 11 */
#define PCM_BURST      64u            /* pairs of room awaited before pushing (about 1.5 ms of audio) */
#define PCM_ST_LEVEL(s) ((s) & 0xFFFu)
#define PCM_ST_EMPTY(s) (((s) >> 16) & 1u)

static inline __attribute__((always_inline)) uint8_t pcm_push_pairs(const pcm_hooks_t *h, const int16_t *pcm, uint32_t n_pairs, uint32_t stereo,
                                                                   int32_t vol_gain, uint32_t *fade_left, uint32_t fade_samples)
{
    uint32_t done = 0;
    while (done < n_pairs) {
        const uint32_t remaining = n_pairs - done;
        const uint32_t need = remaining < PCM_BURST ? remaining : PCM_BURST;
        uint32_t st = h->rd_st();
        uint32_t room = PCM_FIFO_DEPTH - PCM_ST_LEVEL(st);
        h->note(room < need, PCM_ST_EMPTY(st));
        if (room < need) {
            h->wait_begin();
            do {
                if (h->spin()) { h->wait_end(); return 0u; }
                st = h->rd_st();
                room = PCM_FIFO_DEPTH - PCM_ST_LEVEL(st);
            } while (room < need);
            h->wait_end();
        }
        const uint32_t k = room < remaining ? room : remaining;
        for (uint32_t j = 0; j < k; j++, done++) {
            int32_t l = pcm[stereo ? 2u * done : done];
            int32_t r = stereo ? pcm[2u * done + 1u] : l;
            pcm_gain_apply(&l, &r, vol_gain, fade_left, fade_samples);
            h->wr(pcm_pack(l, r));
        }
    }
    return 1u;
}
#endif
