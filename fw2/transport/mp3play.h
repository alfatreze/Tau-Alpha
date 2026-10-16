/* Player 2 S6 MP3 transport: the S4 list walker with a decoder: Helix through the frame driver into the real ring, a timeline segment per track (the P3 accounting runs
 * on the device), the governor deciding how much to decode per pass. A natural end into a next track of the SAME rate goes straight on (no flush: gapless by construction);
 * a different rate drains the ring first. Keys: A play, B hold/resume, Left/Right previous/next (flush), X shuffle. */
#ifndef P2_MP3PLAY_H
#define P2_MP3PLAY_H
#include <stdint.h>
#include "listplay.h"
#include "../cores/timeline_core.h"
#include "../cores/governor_core.h"
typedef struct {
    uint32_t gen, rate, playing, held, cur_valid, cur_gen, cur_pos, acct_bad, stalls, gov_mode;
    uint32_t state, frames, errors, resyncs, pairs_out, pairs_total, seg_put, gapless_next, drain_next, rate_changes, ended;
    uint32_t dec_max_cyc, dec_steps, need_data, ch, init_fail, non_mp3, seeks, seek_fail, seek_ign, seek_to;
} mp_t;
extern mp_t mp;
extern tl_t mp_tl;
extern gov_t mp_gov;
void mp3play_init(int cymo_present);
void mp3play_key(uint32_t edge);
void mp3play_pump(void);
void mp3play_heard(void);
#endif
