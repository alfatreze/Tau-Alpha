/* fw2/cores/timeline_core.h -- Player 2 timeline (docs/features/PLAYER_V2_SPEC.md sections 3 (P2, P3, P4) and 5). Pure logic: no MMIO, host-tested (sim/test_p2_timeline.py).
 *
 * The timeline is a short queue of SEGMENTS. A segment is a contiguous run of the PCM ring that belongs to one track at one speed. Everything position-related is derived
 * from the queue and ONE hardware number, `heard`: the pairs the output side has consumed, in ring-push-count units (R_RING_CNT minus the FIFO level). The domain is a
 * free-running uint32_t; every comparison is a signed difference, valid while two compared values are less than 2^31 apart (about 13 hours of audio).
 *
 *   put side (the producer, one writer):  tl_open() when a track's first pair is about to be put, tl_put() for every block, tl_close() at the end of the track, tl_flush()
 *                                         when the ring is flushed (the unread pairs are dropped; the push count does not move, the next pair gets the same count).
 *   heard side (the UI, one reader):      tl_advance(heard) pops segments the output has left and counts SWAP events; tl_current()/tl_pos() answer "which track, where".
 *
 * Sample accounting (P3): tl_close() compares the pairs put for the segment with the pairs the track should hold. A difference larger than the segment's tolerance
 * is counted (acct_err) and kept (last_delta), never silently accepted. */
#ifndef TIMELINE_CORE_H
#define TIMELINE_CORE_H
#include <stdint.h>

#define TL_MAX 4u

#define TL_OPEN_END 0xFFFFFFFFu      /* ring_end of a segment whose decoder has not reached the end */

enum { TLF_AFTER_FLUSH = 1u, TLF_GAPLESS_IN = 2u, TLF_TEMPO = 4u };

typedef struct {
    uint32_t gen;                /* the track's generation (never reused) */
    uint32_t ring_start;         /* push count of the segment's first pair */
    uint32_t ring_end;           /* push count one past its last pair; TL_OPEN_END until closed */
    uint32_t src_start;          /* source pair index of the first pair (a seek lands mid-track) */
    uint32_t put;                /* pairs put into the ring for this segment */
    uint32_t expect;             /* pairs the track should hold from src_start (0 = unknown: not checked) */
    uint16_t tol;                /* accounting tolerance in pairs (0 for exact formats; a tempo segment rounds) */
    uint16_t num, den;           /* output pairs per source pair = num/den (1/1 plain; tempo 2x speed = 1/2) */
    uint8_t  flags;
} tl_seg_t;

typedef struct {
    tl_seg_t s[TL_MAX];
    uint8_t  head, n;            /* s[head .. head+n-1] (mod TL_MAX), oldest first */
    uint32_t put_total;          /* push count the next put pair will get (the one writer of this quantity) */
    uint32_t swaps;              /* SWAP events delivered by tl_advance() */
    uint32_t acct_err;           /* segments closed with |put - expect| > tol */
    int32_t  last_delta;         /* put - expect of the last segment that failed */
    uint32_t overflow;           /* tl_open() calls refused: the queue was full */
    uint32_t flushes;
    uint32_t empty;              /* segments closed with no pair (dropped from the queue) */
} tl_t;

#ifndef BUG
#define BUG 0
#endif

static inline void tl_init(tl_t *t, uint32_t push_count_now)
{
    t->head = 0; t->n = 0; t->put_total = push_count_now;
    t->swaps = 0; t->acct_err = 0; t->last_delta = 0; t->overflow = 0; t->flushes = 0; t->empty = 0;
}
static inline tl_seg_t *tl_at(tl_t *t, uint8_t i) { return &t->s[(uint8_t)(t->head + i) % TL_MAX]; }
static inline const tl_seg_t *tl_atc(const tl_t *t, uint8_t i) { return &t->s[(uint8_t)(t->head + i) % TL_MAX]; }

/* Begin a segment at the current put count. Returns 0 when the queue is full (counted). */
static inline tl_seg_t *tl_open(tl_t *t, uint32_t gen, uint32_t src_start, uint32_t expect, uint16_t tol, uint16_t num, uint16_t den, uint8_t flags)
{
    if (t->n >= TL_MAX) { t->overflow++; return 0; }
    tl_seg_t *g = tl_at(t, t->n); t->n++;
    g->gen = gen; g->ring_start = t->put_total; g->ring_end = TL_OPEN_END;
#if BUG == 5
    g->src_start = 0;
#else
    g->src_start = src_start;
#endif
    g->put = 0; g->expect = expect; g->tol = tol; g->num = num ? num : 1u; g->den = den ? den : 1u; g->flags = flags;
    return g;
}

/* `pairs` were put for the newest (open) segment. */
static inline void tl_put(tl_t *t, uint32_t pairs)
{
    if (!t->n) return;
#if BUG == 6
    tl_at(t, 0)->put += pairs;
#else
    tl_at(t, (uint8_t)(t->n - 1u))->put += pairs;
#endif
#if BUG == 7
    /* the put count forgets the pairs: the next segment starts at the wrong place */
#else
    t->put_total += pairs;
#endif
}

/* The decoder reached the end of the newest segment. Returns 0 if the accounting holds, 1 if not. */
static inline int tl_close(tl_t *t)
{
    if (!t->n) return 0;
    tl_seg_t *g = tl_at(t, (uint8_t)(t->n - 1u));
    g->ring_end = t->put_total;
    if (!g->put) {               /* a track that produced no pair is never heard: it leaves the queue (no swap to it and back), the accounting still judges it */
#if BUG != 9
        t->n--; t->empty++;
#endif
    }
    if (!g->expect) return 0;
    int32_t d = (int32_t)(g->put - g->expect);
#if BUG == 2
    d = 0;                       /* the check never fires */
#endif
#if BUG == 8
    if (d > 0) d = 0;            /* only a shortfall is reported: an overlong segment passes */
#endif
    if ((d < 0 ? -d : d) > (int32_t)g->tol) { t->acct_err++; t->last_delta = d; return 1; }
    return 0;
}

/* The ring was flushed while `push_count_now` pairs had been pushed: everything unread is gone, the next pair gets push count push_count_now. */
static inline void tl_flush(tl_t *t, uint32_t push_count_now)
{
    t->head = 0; t->n = 0; t->flushes++;
#if BUG == 4
    (void)push_count_now;        /* keeps the old put count: the next segment starts in the future */
#else
    t->put_total = push_count_now;
#endif
}

/* The segment the output side is inside: the one with ring_start <= heard < ring_end. NULL when nothing is queued or heard is before the first segment. */
static inline const tl_seg_t *tl_current(const tl_t *t, uint32_t heard)
{
    for (uint8_t i = 0; i < t->n; i++) {
        const tl_seg_t *g = tl_atc(t, i);
#if BUG == 3
        int after_start = heard >= g->ring_start;                   /* unsigned: wrong across the 2^32 wrap */
        int before_end = g->ring_end == TL_OPEN_END || heard < g->ring_end;
#elif BUG == 1
        int after_start = (int32_t)(heard - g->ring_start) > 0;      /* the first pair of a segment is not "heard" yet */
        int before_end = g->ring_end == TL_OPEN_END || (int32_t)(heard - g->ring_end) < 0;
#else
        int after_start = (int32_t)(heard - g->ring_start) >= 0;
        int before_end = g->ring_end == TL_OPEN_END || (int32_t)(heard - g->ring_end) < 0;
#endif
        if (after_start && before_end) return g;
    }
    return 0;
}

/* Called as `heard` advances. Pops every segment the output has fully left (its ring_end <= heard) and counts a SWAP for each pop that leaves a segment queued.
 * Returns the number of swaps delivered by this call. */
static inline uint32_t tl_advance(tl_t *t, uint32_t heard)
{
    uint32_t sw = 0;
    while (t->n) {
        const tl_seg_t *g = tl_atc(t, 0);
        if (g->ring_end == TL_OPEN_END || (int32_t)(heard - g->ring_end) < 0) break;
        t->head = (uint8_t)((t->head + 1u) % TL_MAX); t->n--;
        if (t->n) { sw++; t->swaps++; }
    }
    return sw;
}

/* Position in the source of the pair about to be heard, in source pairs. One formula for plain play, a seek, a gapless swap and tempo (section 5 of the spec).
 * Plain segments (1/1) stay 32-bit; a ratio uses a 64-bit product (the divide is one per UI tick). */
static inline uint32_t tl_pos(const tl_seg_t *g, uint32_t heard)
{
    uint32_t off = (uint32_t)(heard - g->ring_start);
    if (g->num == g->den) return g->src_start + off;
    return g->src_start + (uint32_t)(((uint64_t)off * g->den) / g->num);
}
#endif
