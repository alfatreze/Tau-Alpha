/* The Player 2 FLAC decoder as its own compilation unit, built for SIZE: the 192 KB link has no room for the recovery, open and seek code at -O2 (it grew the image by 16 KB inlined into the transport, S8-2).
 * Only the sample loops (Rice, predictors, CRC) are worth speed; they are marked below. The transport calls the flac2_* wrappers and sees the track core's types only. */
#pragma GCC optimize ("Os")
#define F2_HOT __attribute__((optimize("O2")))
#include "../cores/flac2_track_core.h"
#include "flac2_unit.h"
void flac2_init(f2t_t *t, uint8_t *seek_buf, uint32_t seek_cap, int32_t *frame_buf, uint32_t frame_cap) { f2t_init(t, seek_buf, seek_cap, 0, 0, frame_buf, frame_cap, F2_FLAG_CRC16); }
void flac2_begin(f2t_t *t, stream_t *s, uint32_t file_size) { f2t_begin(t, s, file_size); }
int  flac2_step(f2t_t *t, stream_t *s, const uint8_t *ring, int32_t *out, uint32_t outcap, uint32_t maxres, uint32_t *np, int *ev) { return f2t_step(t, s, ring, out, outcap, maxres, np, ev); }
int  flac2_seek(f2t_t *t, stream_t *s, uint64_t target) { return f2t_seek(t, s, target); }
