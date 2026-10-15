/* Player 2 S3 tone transport: a continuous 1 kHz sine played as 12-second SEGMENTS through the real ring, with the timeline, the governor and the sample accounting
 * running exactly as the real transport will use them. A test instrument: the segments are gapless by construction (no flush between them), so a gap or a phase jump at
 * a boundary on the Pocket is a defect of the output path, not of any decoder. Keys: A start/restart, B hold/resume, X flush (skip), Y switch 44.1/48 kHz (flushes). */
#ifndef P2_TONE_H
#define P2_TONE_H
#include <stdint.h>
#include "../cores/timeline_core.h"
#include "../cores/governor_core.h"

typedef struct {
    uint32_t gen;                 /* generation of the segment being produced (never reused) */
    uint32_t seg_pairs;           /* pairs per segment at the current rate (12 s) */
    uint32_t seg_left;            /* pairs still to produce in the open segment */
    uint32_t src_pos;             /* source pair index of the next pair of the open segment */
    uint32_t phase;               /* the sine's phase accumulator: continuous across segments */
    uint32_t rate;                /* 44100 or 48000 */
    uint8_t  playing, held, seg_open;
    /* what the heard side sees (written by tone_heard() only) */
    uint32_t cur_gen, cur_pos, cur_valid;
    uint32_t blocks, acct_bad, stalls;
    uint32_t gov_mode;
} tone_t;

extern tone_t tone;
extern tl_t   tone_tl;
extern gov_t  tone_gov;

void tone_init(int cymo_present);
void tone_key(uint32_t edge);
void tone_pump(void);             /* produce while the governor wants it: at most 4 blocks per call */
void tone_heard(void);            /* advance the timeline to the heard position and publish which segment is heard */
#endif
