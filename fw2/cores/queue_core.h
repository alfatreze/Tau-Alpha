/* fw2/cores/queue_core.h -- Player 2 play queue (docs/features/PLAYER_V2_SPEC.md section 10). Pure logic, host-tested (sim/test_p2_queue.py).
 *
 * The queue knows nothing about playing: transport asks it for a track id. It holds the play order (u16 library track ids in a caller-owned array), the position,
 * repeat mode, a seeded shuffle and a short history of what was actually played (so "previous" in shuffle mode goes back to what was heard, not to the neighbour in
 * the shuffled order). The shuffle is the library's own (xorshift32, Fisher-Yates from the end), so a seed reproduces an order bit for bit. */
#ifndef QUEUE_CORE_H
#define QUEUE_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif

#define Q_NONE   0xFFFFu
#define Q_HIST   32u
typedef enum { RPT_OFF = 0, RPT_ALL = 1, RPT_ONE = 2 } q_repeat_t;

typedef struct {
    uint16_t *ids;            /* the play order */
    uint32_t  n, pos;
    uint8_t   repeat;         /* q_repeat_t */
    uint8_t   shuffled;
    uint32_t  seed;
    uint16_t  hist[Q_HIST];   /* ids played before the current one, most recent last */
    uint32_t  hist_n;
    uint32_t  advances, wraps, backs;
} queue_t;

static inline uint32_t q_rng(uint32_t *s) { uint32_t x = *s ? *s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }

static inline void queue_set(queue_t *q, uint16_t *ids, uint32_t n)
{
    q->ids = ids; q->n = n; q->pos = 0; q->shuffled = 0; q->hist_n = 0;
}
static inline uint16_t queue_cur(const queue_t *q) { return q->n ? q->ids[q->pos] : Q_NONE; }

/* The id `k` entries ahead of the current one (k = 0 is the current), following repeat: RPT_ALL wraps, RPT_ONE always answers the current, RPT_OFF ends. */
static inline uint16_t queue_peek(const queue_t *q, uint32_t k)
{
    if (!q->n) return Q_NONE;
    if (q->repeat == RPT_ONE) return q->ids[q->pos];
    uint32_t p = q->pos + k;
    if (p >= q->n) { if (q->repeat != RPT_ALL) return Q_NONE; p %= q->n; }
    return q->ids[p];
}

static inline void q_hist_push(queue_t *q, uint16_t id)
{
    if (q->hist_n == Q_HIST) { for (uint32_t i = 1; i < Q_HIST; i++) q->hist[i - 1] = q->hist[i]; q->hist_n--; }
    q->hist[q->hist_n++] = id;
}

/* Fisher-Yates from the end with the library's generator. keep_current moves the playing track to index 0 first and shuffles only the rest, so turning shuffle on
 * does not change what is playing. keep_current = 0 equals lib_queue_shuffle_all bit for bit. */
static inline void queue_shuffle(queue_t *q, uint32_t seed, int keep_current)
{
    uint32_t from = 0;
    if (keep_current && q->n) {
        uint16_t t = q->ids[0]; q->ids[0] = q->ids[q->pos]; q->ids[q->pos] = t;
#if BUG != 1
        from = 1;
#endif
        q->pos = 0;
    }
    q->seed = seed; q->shuffled = 1;
    uint32_t m = q->n - from;
    for (uint32_t i = m; i > 1u; i--) {
        uint32_t j = q_rng(&seed) % i;
#if BUG == 2
        j = q_rng(&seed) % (i + 1u > m ? i : i + 1u);                 /* biased/overrun index */
#endif
        uint16_t t = q->ids[from + i - 1u]; q->ids[from + i - 1u] = q->ids[from + j]; q->ids[from + j] = t;
    }
}

/* Back to a natural order: the caller rebuilds `orig` (an album, a list, the whole library) and the queue keeps pointing at the same track. */
static inline void queue_restore(queue_t *q, const uint16_t *orig, uint32_t n)
{
    uint16_t cur = queue_cur(q);
    q->n = n; q->shuffled = 0;
    for (uint32_t i = 0; i < n; i++) q->ids[i] = orig[i];
    q->pos = 0;
    for (uint32_t i = 0; i < n; i++) if (orig[i] == cur) { q->pos = i; break; }
}

/* Move forward (auto = the track ended) or manually next. Returns the new current id, or Q_NONE at the end of a non-repeating queue (pos is left on the last track).
 * RPT_ONE repeats the current track on auto and moves on for a manual next, like every player. */
static inline uint16_t queue_next(queue_t *q, int auto_advance)
{
    if (!q->n) return Q_NONE;
#if BUG == 3
    if (q->repeat == RPT_ONE)                                   /* a manual next also repeats */
#else
    if (auto_advance && q->repeat == RPT_ONE)
#endif
    { q->advances++; return q->ids[q->pos]; }
    if (q->pos + 1u >= q->n) {
        if (q->repeat == RPT_OFF) return Q_NONE;
        q->wraps++;
        if (q->shuffled) { uint16_t last = q->ids[q->pos]; queue_shuffle(q, q->seed + 0x9E3779B9u, 0);
            if (BUG != 5 && q->n > 1u && q->ids[0] == last) { uint16_t t = q->ids[0]; q->ids[0] = q->ids[1]; q->ids[1] = t; }          /* the same track must not play twice across the seam */
            q->pos = 0; q_hist_push(q, last); q->advances++; return q->ids[0]; }
        q_hist_push(q, q->ids[q->pos]); q->pos = 0; q->advances++; return q->ids[0];
    }
    q_hist_push(q, q->ids[q->pos]);
    q->pos++; q->advances++;
    return q->ids[q->pos];
}

/* Previous: in shuffle mode the track actually heard before (history), otherwise the neighbour. Q_NONE at the start of a non-repeating queue. */
static inline uint16_t queue_prev(queue_t *q)
{
    if (!q->n) return Q_NONE;
    if (q->shuffled && q->hist_n) {
        #if BUG == 4
        uint16_t id = q->hist[q->hist_n - 1u];
#else
        uint16_t id = q->hist[--q->hist_n];
#endif
        for (uint32_t i = 0; i < q->n; i++) if (q->ids[i] == id) { q->pos = i; q->backs++; return id; }
    }
    if (q->pos == 0) {
        if (q->repeat == RPT_OFF) return Q_NONE;
        q->pos = q->n - 1u; q->backs++; return q->ids[q->pos];
    }
    if (!q->shuffled && q->hist_n) q->hist_n--;
    q->pos--; q->backs++;
    return q->ids[q->pos];
}
/* Jump to a specific track already in the queue (the user picked it from the list). */
static inline int queue_goto(queue_t *q, uint16_t id)
{
    for (uint32_t i = 0; i < q->n; i++) if (q->ids[i] == id) { q_hist_push(q, q->ids[q->pos]); q->pos = i; return 1; }
    return 0;
}
#endif
