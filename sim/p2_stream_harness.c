/* Host harness for fw2/cores/stream_core.h: a time-stepped model of the card (one command at a time, per-read latency with jitter and a long outlier, reads past the
 * end failing, transient failures right after an open) and of a consumer that needs a frame of bytes at a fixed rate and checks every byte against the file.
 * Args: known size rate_Bps frame lat_us jit_us outlier_read outlier_ms settle_fails seek_ms seek_to maxms cap seed
 * Output: key=value lines. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CLK_HZ_ST 66000000u        /* the model's 66 cycles per microsecond */
#include "stream_core.h"

#define STEP_US 100u
#define CYC_PER_US 66u            /* close enough to 66.667 MHz for the model */
static uint8_t file_byte(uint32_t i, uint32_t seed) { return (uint8_t)((i * 31u + 7u) ^ (i >> 8) ^ seed); }
static uint32_t lcg(uint32_t *x) { *x = *x * 1664525u + 1013904223u; return *x >> 8; }
int main(int argc, char **argv)
{
    if (argc < 15) return 2;
    int known = atoi(argv[1]); uint32_t size = (uint32_t)atoi(argv[2]), rate = (uint32_t)atoi(argv[3]), frame = (uint32_t)atoi(argv[4]);
    uint32_t lat = (uint32_t)atoi(argv[5]), jit = (uint32_t)atoi(argv[6]); int outl = atoi(argv[7]); uint32_t outl_ms = (uint32_t)atoi(argv[8]);
    int settle_fails = atoi(argv[9]); int seek_ms = atoi(argv[10]); uint32_t seek_to = (uint32_t)atoi(argv[11]); uint32_t maxms = (uint32_t)atoi(argv[12]);
    uint32_t cap = (uint32_t)atoi(argv[13]), seed = (uint32_t)atoi(argv[14]);
    static uint8_t ring[65536]; static stream_t s; uint32_t rng = seed * 2654435761u + 1u;
    stream_init(&s, cap, 5u * 1000000u * CYC_PER_US);
    stream_open(&s, 0, known ? size : 0, 0);
    /* the card */
    struct { int busy; io_req_t r; uint32_t done_at; int will_fail; } card = {0};
    int wrap_viol = 0, nreads = 0, started_while_busy = 0, settle_left = settle_fails, bad = 0, seeked = 0; uint32_t cons_pos = 0, expect_gen_pos = 0; uint64_t t_us = 0;
    uint32_t debt_us_acc = 0; uint32_t steady_min = 0xFFFFFFFFu; uint32_t consumed = 0, starved_us = 0, done_all = 0; uint32_t order_viol = 0;
    uint8_t tmp[4096];
    (void)expect_gen_pos;
    while (t_us < (uint64_t)maxms * 1000u) {
        uint32_t now = (uint32_t)(t_us * CYC_PER_US);
        io_result_t res = IO_PENDING;
        if (card.busy && t_us >= card.done_at) {
            res = card.will_fail ? IO_FAIL : IO_OK;
            if (!card.will_fail) {                                 /* the DMA lands (also for a stale read: it writes where it was told to) */
                uint32_t src = card.r.off;
                for (uint32_t i = 0; i < card.r.len; i++) ring[(card.r.dst + i) % cap] = file_byte(src + i, seed);
            }
            card.busy = 0;
        }
        io_req_t rq;
        if (stream_pump(&s, now, res, &rq)) {
            if (card.busy) started_while_busy++;
            if (rq.dst + rq.len > cap) wrap_viol++;
            card.busy = 1; card.r = rq; nreads++;
            uint32_t l = lat + (jit ? lcg(&rng) % jit : 0);
            if (outl >= 0 && nreads == outl) l += outl_ms * 1000u;
            card.done_at = (uint32_t)t_us + l;
            uint32_t end = known ? size : size;                      /* the real file size: reading past it fails as a whole */
            card.will_fail = (rq.off + rq.len > end);
            if (!card.will_fail && settle_left > 0) { settle_left--; card.will_fail = 1; }
        }
        if (seek_ms >= 0 && !seeked && t_us >= (uint64_t)seek_ms * 1000u) { seeked = 1; stream_seek(&s, seek_to); cons_pos = seek_to; }
        /* the consumer: needs `frame` bytes every frame*1e6/rate us */
        debt_us_acc += STEP_US;
        uint32_t period_us = (uint32_t)((uint64_t)frame * 1000000u / rate);
        if (debt_us_acc >= period_us || done_all == 0) {
            if (debt_us_acc >= period_us) {
                uint32_t left = (known ? size : size) - cons_pos; uint32_t n = frame < left ? frame : left;
                if (!n) { done_all = 1; break; }
                if (s.eof && s.avail && s.avail < n) n = s.avail;           /* the last, partial frame */
                if (stream_ready(&s, n)) {
                    stream_take(&s, ring, tmp, n);
                    if (!s.eof && s.avail < steady_min) steady_min = s.avail;
                    for (uint32_t i = 0; i < n; i++) if (tmp[i] != file_byte(cons_pos + i, seed)) bad++;
                    cons_pos += n; consumed += n; debt_us_acc -= period_us;
                } else if (s.eof && s.avail < n) { done_all = 2; break; }
                else starved_us += STEP_US;
                if (debt_us_acc > 20u * period_us) debt_us_acc = 20u * period_us;
            }
        }
        t_us += STEP_US;
    }
    (void)order_viol;
    printf("wrap=%d\nsteady_min=%u\nbad=%d\nstarted_busy=%d\nconsumed=%u\npos=%u\ndone=%u\neof=%d\nerr=%d\nreads=%u\nfails=%u\nretries=%u\ntimeouts=%u\nstale=%u\nstarved=%u\nstarved_us=%u\nmin_avail=%u\nmax_lat=%u\nsize_final=%u\nt_ms=%u\nseeks=%u\n",
           wrap_viol, steady_min == 0xFFFFFFFFu ? 0u : steady_min, bad, started_while_busy, consumed, s.pos, done_all, s.eof, s.err, s.reads, s.fails_total, s.retries, s.timeouts, s.stale, s.starved, starved_us,
           s.min_avail == 0xFFFFFFFFu ? 0u : s.min_avail, s.max_lat / CYC_PER_US, s.size, (unsigned)(t_us / 1000u), s.seeks);
    printf("hist=%u,%u,%u,%u,%u,%u,%u\nfresh_us=%u\nstale_us=%u\nslow_stale=%u\nslow_off=%u\n", s.lat_hist[0], s.lat_hist[1], s.lat_hist[2], s.lat_hist[3], s.lat_hist[4], s.lat_hist[5], s.lat_hist[6], s.max_lat_fresh / CYC_PER_US, s.max_lat_stale / CYC_PER_US, s.slow_stale, s.slow_off);
    return 0;
}
