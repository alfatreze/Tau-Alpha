/* The player's list state, shared by the transport and the views: what is playing from the queue, shuffle/repeat, and the compressed-stream reader. (Formerly the S4 list walker's state, with its test
 * counters; those are archived in archive/player2_bringup/.) */
#ifndef P2_PLAYER_STATE_H
#define P2_PLAYER_STATE_H
#include <stdint.h>
#include "../cores/stream_core.h"
#include "../cores/queue_core.h"
typedef struct {
    uint32_t state;               /* 0 idle, 1 playing */
    uint32_t cur_id, cur_pos, qn, shuffle, repeat;
    uint32_t expect;              /* the size the library lists for the current file */
    uint32_t open_err, skipped;   /* tracks that could not be opened */
} lp_t;
extern lp_t lp;
extern stream_t lp_stream;
#endif
