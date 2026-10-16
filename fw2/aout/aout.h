/* Player 2 audio output (spec section 7): the CPU's whole interface to the PCM ring, the hardware gain stage and the output rate. Owns RING_*, PCM_RATE, GAIN_* and CYMO_CTRL (P7).
 * Producer side only puts RAW pairs into the ring and publishes the write index once per block; the reader, FIFO, gain stage, Cymo resampler and Halcyon are hardware. */
#ifndef P2_AOUT_H
#define P2_AOUT_H
#include <stdint.h>

#define AOUT_RING_PAIRS   52000u         /* allocated */
#define AOUT_RING_MARGIN  3328u          /* kept untouched behind the read index: the FIFO and the meters' read-back window */
#define AOUT_USABLE       (AOUT_RING_PAIRS - 1u - AOUT_RING_MARGIN)   /* 48,672 pairs: 1.10 s at 44.1 kHz */

typedef struct { uint32_t cnt, fifo, ridx, wptr; } aout_snap_t;    /* one coherent sample: cnt is stable across the reads */

typedef struct {
    uint32_t put_blocks, put_pairs, put_stalls;    /* a stall = fewer pairs accepted than offered (the ring was full) */
    uint32_t put_max_cyc;
    uint32_t flushes;
    uint32_t ring_min, ring_max;                   /* ring fill extremes since the ring was armed (sampled by aout_poll) */
    uint32_t under_ring;                           /* FIFO empty while the ring still held pairs: the reader fell behind (must stay 0) */
    uint32_t dry;                                  /* FIFO and ring both empty while playing: the producer fell behind */
} aout_stats_t;
extern aout_stats_t aout_stats;

/* 0 = armed; 1 = the bitstream has no ring; 2 = the PSRAM window failed its proof. Clears the hold bit: the reader runs as soon as a pair is published. */
int      aout_init(void);
void     aout_set_rate(uint32_t hz, int cymo_present);   /* the output tick, and the Cymo resampler guard (live only for 44.1 kHz) */
void     aout_gain_unity(void);                          /* hardware gain stage on, target unity, no ramp from a stale value */
uint32_t aout_room(void);                                /* pairs that can be put now */
uint32_t aout_put(const int16_t *pcm, uint32_t pairs, int stereo);   /* returns the pairs accepted (fewer than offered when the ring is full) */
aout_snap_t aout_snap(void);
static inline uint32_t aout_fill(const aout_snap_t *s) { return s->wptr >= s->ridx ? s->wptr - s->ridx : s->wptr + AOUT_RING_PAIRS - s->ridx; }
static inline uint32_t aout_heard(const aout_snap_t *s) { return s->cnt - s->fifo; }   /* pairs the output side has consumed, in push-count units */
void     aout_flush(void);                               /* drops the unread ring and the FIFO; the next put pair gets the push count aout_snap().cnt */
void     aout_mute_wait(void);                           /* gain target to silence and wait out the stage's 5 ms ramp (the FIFO still holds 23+ ms): flush or hold after this is inaudible */
void     aout_fade_in(uint32_t target_q15);              /* gain target to `target`; the stage ramps up from where it is (silence after aout_mute_wait) */
void     aout_gain_set(uint32_t target_q15);              /* a new target: the stage ramps (5 ms for full scale), no click */
void     aout_hold(int on);                              /* pause: the reader stops */
void     aout_poll(int playing);                         /* once per loop: updates the stats from one snapshot */
#endif
