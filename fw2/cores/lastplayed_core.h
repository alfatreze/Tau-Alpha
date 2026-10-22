/* fw2/cores/lastplayed_core.h -- the last played track, kept across a power cycle (B-808). Pure: no registers. Host-tested (sim/test_p2_lastplayed.py).
 *
 * Three words of the persisted register file (settings_core.h SF_LAST0..2, persist ids 24..26): how the queue was made (kind and parent: an album, a list, everything), the track, and a check word. The queue is rebuilt in
 * its NATURAL order from kind and parent at boot, the track is found in it, and playback is left stopped on it (A plays): a shuffled order is not kept (a seed would not survive a library change). A word that does not
 * check out (a stale one, another core's, a library that changed so the track is no longer in the queue) restores nothing. */
#ifndef LASTPLAYED_CORE_H
#define LASTPLAYED_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint32_t valid, kind, parent, id; } lastp_t;
#define LASTP_MAGIC 0x4C505431u                 /* 'LPT1' */
static inline uint32_t lastp_check(uint32_t w0, uint32_t w1) { return (w0 ^ ((w1 << 7) | (w1 >> 25)) ^ LASTP_MAGIC); }
static inline void lastp_pack(const lastp_t *l, uint32_t *w)
{
    if (!l->valid) { w[0] = w[1] = w[2] = 0u; return; }
    w[0] = 0x80000000u | ((l->kind & 15u) << 24) | (l->parent & 0xFFFFu);
    w[1] = l->id;
    w[2] = lastp_check(w[0], w[1]);
}
/* 1 and the fields when the three words are a valid record */
static inline int lastp_unpack(const uint32_t *w, lastp_t *l)
{
    l->valid = 0u; l->kind = l->parent = l->id = 0u;
    if (!(w[0] & 0x80000000u)) return 0;
#if BUG != 1
    if (w[2] != lastp_check(w[0], w[1])) return 0;
#endif
    l->valid = 1u; l->kind = (w[0] >> 24) & 15u; l->parent = w[0] & 0xFFFFu; l->id = w[1];
    return 1;
}
/* the position of track `id` in a queue of n tracks (the queue of this kind and parent as built now), or -1 */
static inline int32_t lastp_find(const uint16_t *ids, uint32_t n, uint32_t id)
{
#if BUG == 2
    for (uint32_t i = 0; i + 1u < n; i++) if (ids[i] == id) return (int32_t)i;          /* misses the last track */
#else
    for (uint32_t i = 0; i < n; i++) if (ids[i] == id) return (int32_t)i;
#endif
    return -1;
}
#endif
