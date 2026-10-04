/* fw/pcm_push.h -- the pure part of the one shared PCM push path (Cymo C1, B-533).
 *
 * MP3 (the decode loop) and FLAC (flac_emit) each carried their own copy of: volume, then the discontinuity fade-in, then pack and write. They had already drifted
 * apart once (FLAC shipped with no volume at all). This header holds the arithmetic exactly as it was in both copies, so it can be host-tested against the old
 * formula; the FIFO wait and the MMIO write stay in player.c's cymo_push(). Volume first, then the fade, so a fade-in at low volume stays at low volume. Capped
 * at unity: it only ever attenuates, so no clamp is needed (volume, below, is a dB taper with a ramp since B-598). */
#ifndef PCM_PUSH_H
#define PCM_PUSH_H
#include <stdint.h>

/* ---- Volume (Cymo C1, B-598): a dB taper and a click-free ramp ----------------------------------------------------------------------------------------
 * Volume is a position 0..100. Position 100 is 0 dB, each position below it is 0.6 dB quieter (position 1 is -59.4 dB) and position 0 is mute: gain(v) = 10^((v-100)*0.6/20)
 * as a Q15 factor (32768 = unity; a 16-bit sample times a Q15 gain stays inside int32). The table is the formula rounded to the nearest integer (sim/test_pcm_push.py recomputes it).
 * A change of volume does not jump: `cur` moves toward `target` by PCM_VOL_RAMP per sample pair (full scale in about 220 pairs, 5 ms at 44.1 kHz), so a step is a short smooth
 * slope instead of a click. Steady state is one compare per pair. Boot snaps `cur` to `target` (vol_apply_snap in player.c) so the first samples are not a ramp from full volume. */
typedef struct { int32_t cur, target; } pcm_vol_t;
#define PCM_VOL_UNITY 32768
#define PCM_VOL_RAMP  149
static const uint16_t pcm_vol_tab[101] = {
    0, 35, 38, 40, 43, 46, 50, 53, 57, 61,
    65, 70, 75, 80, 86, 92, 99, 106, 114, 122,
    130, 140, 150, 160, 172, 184, 197, 212, 227, 243,
    260, 279, 299, 320, 343, 368, 394, 422, 452, 485,
    519, 556, 596, 639, 685, 734, 786, 842, 903, 967,
    1036, 1110, 1190, 1275, 1366, 1464, 1568, 1681, 1801, 1930,
    2068, 2215, 2374, 2544, 2726, 2920, 3129, 3353, 3593, 3850,
    4125, 4420, 4736, 5075, 5438, 5827, 6244, 6690, 7169, 7682,
    8231, 8820, 9450, 10126, 10851, 11627, 12458, 13349, 14304, 15327,
    16423, 17597, 18856, 20205, 21650, 23198, 24857, 26635, 28540, 30581,
    32768,
};
static inline int32_t pcm_vol_target(uint32_t step) { return step > 100u ? PCM_VOL_UNITY : (int32_t)pcm_vol_tab[step]; }

static inline void pcm_gain_apply(int32_t *l, int32_t *r, pcm_vol_t *v, uint32_t *fade_left, uint32_t fade_samples)
{
    if (v->cur != v->target) {
        int32_t d = v->target - v->cur;
        if (d > PCM_VOL_RAMP) d = PCM_VOL_RAMP; else if (d < -PCM_VOL_RAMP) d = -PCM_VOL_RAMP;
        v->cur += d;
    }
    if (v->cur != PCM_VOL_UNITY) {
        *l = (*l * v->cur) >> 15;
        *r = (*r * v->cur) >> 15;
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
                                                                   pcm_vol_t *vol, uint32_t *fade_left, uint32_t fade_samples)
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
            pcm_gain_apply(&l, &r, vol, fade_left, fade_samples);
            h->wr(pcm_pack(l, r));
        }
    }
    return 1u;
}
#endif
