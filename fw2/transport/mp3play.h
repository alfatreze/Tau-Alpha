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
    uint32_t dec_max_cyc, dec_steps, need_data, ch, init_fail, non_mp3, seeks, seek_fail, seek_ign, seek_to, volume, rg_mode, gain_tgt, rg_factor, rg_cap, dec_us, dec_blocks, dec_avg_us, poly_on, poly_slots, poly_mismatch, poly_timeout, tempo_idx, seek_idx, skip_on, skip_lead, skip_tail, skip_applied, skip_ign, tempo_out, flac_waits, flac_refused, flac_err, lpc_slots, lpc_timeout, seg_put_track,
             poly_fail, poly_silent, f2_ferr, f2_resync, f2_silent, f2_wraps, f2_probes, f2_repos, f2_staged_max, f2_rollbacks;          /* the new FLAC decoder (P2_FLAC2): damaged frames, resyncs, silent pairs, bridge uses, seek probes, bytes skipped by repositioning */
} mp_t;
extern mp_t mp;
extern tl_t mp_tl;
extern gov_t mp_gov;
void mp3play_init(int cymo_present);
void mp3play_key(uint32_t edge);
enum { MPA_PLAYPAUSE = 1, MPA_RESTART, MPA_NEXT, MPA_PREV, MPA_SEEK, MPA_VOL, MPA_ALBUM, MPA_REPEAT, MPA_SHUFFLE, MPA_RG, MPA_TEMPO };
void mp3play_act(uint32_t act, int32_t arg);      /* arg: seconds (seek), steps (volume, tempo), +1/-1 (album) */
void mp3play_pump(void);
void mp3play_heard(void);
/* What the Now Playing view shows, refreshed by mp3play_heard() (text only when the heard track changes). */
typedef struct {
    char title[48], artist[40], album[40], quality[32], cover[120];
    uint32_t gen, id, pos, total, rate;          /* pos/total in source pairs of the HEARD track; total 0 = unknown; gen changes with the track */
    uint32_t playing, held, volume, rg_mode, repeat, shuffle, tempo_q8, valid, gov, fault;       /* fault: the required MP3 window unit is missing or failed (stereo MP3 would be silent) */
} np_info_t;
const np_info_t *mp3play_np(void);
/* Let the media stream's command in flight finish and issue nothing new until the next pump: the one card command channel is then free for a blocking job (the cover load). */
void mp3play_io_quiesce(void);
/* Play a queue the library just built (u16 ids in library_queue_buf()): n tracks, starting at `start`. `src_kind`/`src_parent` remember how to rebuild its natural order (X shuffle off). */
void mp3play_play_queue(uint32_t n, uint32_t start, uint32_t src_kind, uint32_t src_parent, int shuffled, uint32_t seed);
/* The persisted values (cores/settings_core.h order: volume, repeat, ReplayGain mode). */
void mp3play_settings_get(uint32_t *vals);
void mp3play_settings_set(const uint32_t *vals);
/* The preferences the Settings menu edits (the first three of cores/settings_core.h are also persisted). Values: volume 0..100, ReplayGain 0..2, tempo index 0..7, repeat 0..2, shuffle 0..1, seek step index 0..3 (10/30/60/120 s). */
enum { PREF_VOLUME = 0, PREF_RG, PREF_TEMPO, PREF_REPEAT, PREF_SHUFFLE, PREF_SEEKSTEP, PREF_SKIPSIL, PREF_COUNT };
uint32_t mp3play_pref_get(uint32_t id);
void     mp3play_pref_set(uint32_t id, uint32_t v);
#endif
