/* Player 2 skip-silence lookup (spec 10.1): the album's `tau-skip.bin` (cores/skiptab_core.h is the format) is read through data slot 7 and the track's numbers returned. The one command channel must be free. */
#ifndef P2_SKIPLIST_H
#define P2_SKIPLIST_H
#include <stdint.h>
/* 1 and the numbers (source pairs) when the track has an entry, else 0 (play the whole track). A folder without the file is remembered, so the rest of the album costs nothing. */
int skiplist_lookup(const char *track_path, uint32_t *lead, uint32_t *tail);
extern uint32_t skiplist_hits, skiplist_misses, skiplist_errors;
#endif
