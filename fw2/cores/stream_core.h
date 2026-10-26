/* fw2/cores/stream_core.h -- Player 2 compressed-stream reader (docs/features/PLAYER_V2_SPEC.md section 9.3). Pure logic, host-tested (sim/test_p2_stream.py).
 *
 * One circular byte ring (the protected 24 KB) is filled by READ commands of the single APF target channel. Rules:
 *   - ONE request in flight, ever. Its completion carries the generation it was issued under; open/seek bump the generation, and a completion of an older
 *     generation is dropped and counted (the command cannot be aborted, so a stale read still lands: it can only land in free space, because a new request is not
 *     issued until it has finished).
 *   - Read-ahead: the next read is issued as soon as a whole chunk of free space exists, not when the consumer runs dry (the old flac_pull lesson).
 *   - A read never wraps the ring (it is one DMA run): its length is clipped to the space before the end of the buffer, to the free space and to the file's end.
 *   - End of file: with a known size it is exact (a failing read is retried, then reported as SHORT); with an unknown size a failing read is halved toward the
 *     boundary (down to MIN_READ) and three failures at the same offset mean end of file (right after an open the slot may still be settling).
 *   - A request not completed within TIMEOUT cycles is a TIMEOUT error; the command is orphaned and nothing new is issued until it finishes.
 * The module touches no hardware: the caller starts the read the pump asks for and feeds its result back. */
#ifndef STREAM_CORE_H
#define STREAM_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif

#ifndef CLK_HZ_ST
#define CLK_HZ_ST 66666667u      /* the core clock, for the latency buckets (a host test may override it) */
#endif
#define ST_CHUNK    4096u
#define ST_MIN_READ 512u
#define ST_RETRIES  3u
typedef enum { SE_OK = 0, SE_NOFILE, SE_SHORT, SE_TIMEOUT, SE_STALE, SE_BADPATH } stream_err_t;
typedef enum { IO_PENDING = 0, IO_OK = 1, IO_FAIL = 2 } io_result_t;

typedef struct { uint32_t off, dst, len, gen; } io_req_t;           /* dst is a byte offset into the ring */

typedef struct {
    uint32_t cap;                  /* ring bytes */
    uint32_t gen;                  /* current generation, never reused */
    uint32_t size;                 /* file size, 0 = unknown */
    uint32_t pos;                  /* file offset of the next byte to request */
    uint32_t wr, rd, avail;        /* ring write index, read index, bytes ready */
    uint8_t  eof, open, err;       /* eof: no more bytes will arrive; err: stream_err_t of the last failure (sticky until the next open) */
    uint8_t  min_fails;            /* unknown size: failures at the minimum read length (three mean end of file) */
    uint8_t  fails;                /* consecutive failures at the same offset */
    uint32_t fail_len;             /* after a failure on an unknown-size file: the shortened length to try next (0 = none) */
    uint32_t fail_pos;
    uint8_t  need_latched;         /* a starvation event is counted once per need */
    struct { uint8_t active, orphan; io_req_t r; uint32_t t0; } fl;     /* the request in flight */
    uint32_t timeout;              /* cycles */
    /* counters */
    uint32_t reads, bytes, fails_total, retries, timeouts, stale, starved, opens, seeks, min_avail, max_lat;
    uint32_t chunk;                /* bytes per read (0 = ST_CHUNK); kept across open/seek, set by the transport per codec */
    uint32_t to_off, to_len;       /* the request that last timed out */
    /* read latency (B-879, the 4 s read after a skip): how long each finished read took, by bucket, and the slowest one's circumstances */
    uint32_t lat_hist[7];          /* reads that took < 5 ms, < 20 ms, < 50 ms, < 200 ms, < 1 s, < 3 s, 3 s or more (a stale read counts too) */
    uint32_t max_lat_fresh, max_lat_stale;   /* the slowest read of the current generation, and the slowest whose generation had moved on (issued before a skip or an open) */
    uint32_t slow_off, slow_len, slow_stale; /* the slowest read overall: its file offset, its length, 1 when it was stale */
} stream_t;

static inline void stream_init(stream_t *s, uint32_t cap, uint32_t timeout_cycles)
{
    uint8_t *p = (uint8_t *)s; for (uint32_t i = 0; i < sizeof *s; i++) p[i] = 0;
    s->cap = cap; s->timeout = timeout_cycles; s->gen = 1; s->min_avail = 0xFFFFFFFFu;
}

/* Start (or restart) reading at `off`; size 0 = unknown. Drops everything buffered and invalidates the request in flight. */
static inline void stream_open(stream_t *s, uint32_t off, uint32_t size, int is_seek)
{
    s->gen++;
    s->pos = off; s->size = size; s->wr = s->rd = s->avail = 0; s->eof = 0; s->err = SE_OK; s->fails = 0; s->min_fails = 0; s->fail_len = 0; s->fail_pos = 0xFFFFFFFFu; s->open = 1; s->need_latched = 0;
    if (is_seek) s->seeks++; else s->opens++;
    s->min_avail = 0xFFFFFFFFu;
    if (s->fl.active) s->fl.orphan = 1;                    /* its completion is for an old generation (counted when it lands) */
}
static inline void stream_seek(stream_t *s, uint32_t off) { stream_open(s, off, s->size, 1); }
static inline void stream_close(stream_t *s) { stream_open(s, 0, 0, 0); s->open = 0; s->eof = 1; }

static inline uint32_t st_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* One pass: collect a finished request, then say whether a new one should start. `res` is the platform's answer for the request in flight (IO_PENDING when none or
 * not finished). Returns 1 and fills *req when the caller must start that read now. */
static inline int stream_pump(stream_t *s, uint32_t now, io_result_t res, io_req_t *req)
{
    if (s->fl.active) {
        uint32_t lat = now - s->fl.t0;
        int timed_out = res == IO_PENDING && (int32_t)(lat - s->timeout) >= 0;
        if (res == IO_PENDING && !timed_out) {                        /* still going: nothing else may be issued */
#if BUG == 4
            goto issue;
#else
            return 0;
#endif
        }
        if (timed_out) {                                              /* give up waiting, but the command is still owned by the platform */
            if (!s->fl.orphan) { s->timeouts++; s->err = SE_TIMEOUT; s->fl.orphan = 1; s->to_off = s->fl.r.off; s->to_len = s->fl.r.len; }
            return 0;
        }
        io_req_t r = s->fl.r; int orphan = s->fl.orphan; (void)orphan; s->fl.active = 0; s->fl.orphan = 0;
        {
            const uint32_t ms = lat / (CLK_HZ_ST / 1000u); const int stale_rd = orphan || r.gen != s->gen;
            s->lat_hist[ms < 5u ? 0 : ms < 20u ? 1 : ms < 50u ? 2 : ms < 200u ? 3 : ms < 1000u ? 4 : ms < 3000u ? 5 : 6]++;
#if BUG == 6
            if (lat > s->max_lat_fresh) s->max_lat_fresh = lat;                                  /* stale reads are not told apart */
#else
            if (stale_rd) { if (lat > s->max_lat_stale) s->max_lat_stale = lat; } else if (lat > s->max_lat_fresh) s->max_lat_fresh = lat;
#endif
            if (lat > s->max_lat) { s->max_lat = lat; s->slow_off = r.off; s->slow_len = r.len; s->slow_stale = stale_rd ? 1u : 0u; }
        }
#if BUG == 1
        if (0) { s->stale++; }                                        /* the generation check is missing */
#else
        if (orphan || r.gen != s->gen) { s->stale++; }
#endif
        else if (0) {}                /* an old generation's completion: dropped */
        else if (res == IO_OK) {
            s->reads++; s->bytes += r.len; s->wr = (s->wr + r.len) % s->cap; s->avail += r.len; s->pos += r.len; s->fails = 0; s->min_fails = 0;
            if (s->size && s->pos >= s->size) s->eof = 1;
        } else {                                                      /* failure */
            s->fails_total++;
            if (s->fail_pos != r.off) { s->fail_pos = r.off; s->fails = 0; }
            s->fails++;
            if (s->size) {
                if (s->fails >= ST_RETRIES) { s->err = SE_SHORT; s->eof = 1; } else s->retries++;
            } else if (BUG != 5 && r.len > ST_MIN_READ) {
                s->retries++;                                         /* halve toward the true boundary: a read across the end fails as a whole */
                s->fail_len = r.len / 2u;
            } else if (BUG == 5 || ++s->min_fails >= ST_RETRIES) { s->size = s->pos; s->eof = 1; }     /* three failures at the minimum length: the end */
            else s->retries++;
        }
    }
#if BUG == 4
issue:
    if (!s->open || s->eof) return 0;
#else
    if (!s->open || s->eof || s->fl.active) return 0;
#endif
#if BUG == 3
    if (s->avail) return 0;                                       /* reads only once the ring is dry */
#endif
    uint32_t ch = s->chunk ? s->chunk : ST_CHUNK, free_b = s->cap - s->avail;
    #if BUG == 2
    uint32_t len = st_min(ch, free_b);
#else
    uint32_t len = st_min(ch, st_min(s->cap - s->wr, free_b));
#endif
    if (s->fail_len) len = st_min(len, s->fail_len);
    uint32_t left = s->size ? s->size - s->pos : 0xFFFFFFFFu;
    int to_end = left <= len;
    if (to_end) len = left;
    if (!len) return 0;
    /* only whole chunks, the run up to the buffer end, the file's tail, or a deliberate shortening after a failure: never a sliver for want of space */
    if (!(len == ch || len >= s->cap - s->wr || to_end || s->fail_len)) return 0;
    s->fail_len = 0;
    req->off = s->pos; req->dst = s->wr; req->len = len; req->gen = s->gen;
    s->fl.active = 1; s->fl.orphan = 0; s->fl.r = *req; s->fl.t0 = now;
    return 1;
}

/* Waits an in-flight read out WITHOUT starting another (the caller needs the one command channel for itself). Call it with the platform's answer for the request in flight until it returns 1.
 * stream_pump() marks a NEW request as in flight the moment it returns 1; a caller that does not start it must cancel it, or the stream waits for a read that was never issued (it
 * times out after 5 s and nothing is ever read again: the Rite of Spring lock, DEV 152-155). */
static inline int stream_wait_idle(stream_t *s, uint32_t now, io_result_t res)
{
    io_req_t rq;
#if BUG == 60
    stream_pump(s, now, res, &rq);                                /* the old quiesce: the returned request is dropped but stays marked in flight */
#else
    if (stream_pump(s, now, res, &rq)) s->fl.active = 0;          /* nothing was started: cancel the request just marked */
#endif
    return !s->fl.active;
}
static inline uint32_t stream_avail(const stream_t *s) { return s->avail; }
/* The decoder needs n contiguous-in-time bytes. Counts a starvation event (once per need) when they are not there and more are expected. */
static inline int stream_ready(stream_t *s, uint32_t n)
{
    if (s->avail >= n) { s->need_latched = 0; return 1; }
    if (s->eof) return 0;
    if (!s->need_latched) { s->need_latched = 1; s->starved++; }
    return 0;
}
/* Copy n <= avail bytes out and release them. */
static inline void stream_take(stream_t *s, const uint8_t *buf, uint8_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) { dst[i] = buf[s->rd]; s->rd = s->rd + 1u == s->cap ? 0 : s->rd + 1u; }
    s->avail -= n;
    if (s->open && s->avail < s->min_avail) s->min_avail = s->avail;
}
static inline void stream_skip(stream_t *s, uint32_t n) { s->rd = (s->rd + n) % s->cap; s->avail -= n; if (s->avail < s->min_avail) s->min_avail = s->avail; }
#endif
