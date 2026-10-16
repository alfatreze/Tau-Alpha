/* Player 2 S4 list-play transport: walks the library queue and streams every file through the real stream reader with no decoder, at a chosen consumption rate, so
 * the card path, the open-by-path, the read-ahead and the queue are exercised on a Pocket before any audio depends on them. Keys: A play/restart, B stop,
 * Left/Right previous/next, X shuffle, Y repeat (off/all/one), Up/Down consumption rate (flat out, 40 KB/s, 176 KB/s, 440 KB/s). */
#ifndef P2_LISTPLAY_H
#define P2_LISTPLAY_H
#include <stdint.h>
#include "../cores/stream_core.h"
#include "../cores/queue_core.h"
typedef struct {
    uint32_t state;               /* 0 idle, 1 streaming, 2 opening */
    uint32_t cur_id, cur_pos, qn, rate_sel, rate_bps, shuffle, repeat;
    uint32_t tracks_done, tracks_ok, size_bad, hash_last, hash_bad, hash_seen;
    uint32_t consumed, consumed_total, expect, open_err, skipped;
    uint32_t bytes_total, kbps, t0_cyc;
} lp_t;
extern lp_t lp;
extern stream_t lp_stream;
void listplay_init(void);
void listplay_key(uint32_t edge);
void listplay_pump(void);
#endif
