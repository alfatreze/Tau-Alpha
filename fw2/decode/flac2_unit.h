#ifndef FLAC2_UNIT_H
#define FLAC2_UNIT_H
#include "../cores/flac2_track_core.h"       /* (types and enums only are used by the transport; the functions below are the unit's wrappers) */
void flac2_init(f2t_t *t, uint8_t *seek_buf, uint32_t seek_cap, int32_t *frame_buf, uint32_t frame_cap);
void flac2_begin(f2t_t *t, stream_t *s, uint32_t file_size);
int  flac2_step(f2t_t *t, stream_t *s, const uint8_t *ring, int32_t *out, uint32_t outcap, uint32_t maxres, uint32_t *np, int *ev);
int  flac2_seek(f2t_t *t, stream_t *s, uint64_t target);
#endif
