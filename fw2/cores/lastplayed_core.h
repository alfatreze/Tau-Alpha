/* fw2/cores/lastplayed_core.h -- the last played track, kept across a power cycle (B-808). Pure: no registers. Host-tested (sim/test_p2_lastplayed.py).
 *
 * Every word is at most 31 bits: the Pocket's interact variables for these words are 0..2,147,483,647 (interact.json), and a value above that is clamped or refused by the Pocket, which is how the first version (a flag in
 * bit 31, a full 32-bit check word) never came back on TAU DEV 178. Two words of the persisted register file (settings_core.h SF_LAST0..1, words 19 and 20 = persist ids 31 and 32): how the queue was made (kind and parent: an album, a list, everything), the track (14 bits) and a check. The queue is rebuilt in
 * its NATURAL order from kind and parent at boot, the track is found in it, and playback is left stopped on it (A plays): a shuffled order is not kept (a seed would not survive a library change). A word that does not
 * check out (a stale one, another core's, a library that changed so the track is no longer in the queue) restores nothing. */
#ifndef LASTPLAYED_CORE_H
#define LASTPLAYED_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
typedef struct { uint32_t valid, kind, parent, id; } lastp_t;
#define LASTP_WORDS 2u
#define LASTP_MAGIC 0x4C505431u                 /* 'LPT1' */
#define LASTP_ID_MAX 16383u                     /* the queue holds at most 16384 tracks: 14 bits */
/* Two words of 31 bits: w0 = record flag (bit 30) | kind (29..26) | parent (25..10) | track id low ten bits (9..0); w1 = check (30..4) | track id high four bits (3..0). The check is 27 bits over the rest. */
static inline uint32_t lastp_check(uint32_t w0, uint32_t id_hi) { const uint32_t x = (w0 ^ (id_hi * 0x9E3779B1u) ^ LASTP_MAGIC); return ((x ^ (x >> 15)) & 0x7FFFFFFu); }
static inline void lastp_pack(const lastp_t *l, uint32_t *w)
{
    if (!l->valid || l->id > LASTP_ID_MAX) { w[0] = w[1] = 0u; return; }
    w[0] = 0x40000000u | ((l->kind & 15u) << 26) | ((l->parent & 0xFFFFu) << 10) | (l->id & 0x3FFu);
    w[1] = (lastp_check(w[0], l->id >> 10) << 4) | (l->id >> 10);
}
/* 1 and the fields when the two words are a valid record */
static inline int lastp_unpack(const uint32_t *w, lastp_t *l)
{
    l->valid = 0u; l->kind = l->parent = l->id = 0u;
    if (!(w[0] & 0x40000000u) || ((w[0] | w[1]) >> 31)) return 0;                         /* no record, or a word the Pocket could not have kept (bit 31) */
#if BUG != 1
    if ((w[1] >> 4) != lastp_check(w[0], w[1] & 15u)) return 0;
#endif
    l->valid = 1u; l->kind = (w[0] >> 26) & 15u; l->parent = (w[0] >> 10) & 0xFFFFu; l->id = ((w[1] & 15u) << 10) | (w[0] & 0x3FFu);
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
