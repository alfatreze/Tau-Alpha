#include "listplay.h"
#include "../platform/mmio.h"
#include "../stream/slot.h"
#include "../stream/library.h"

extern char _ring_start;
#define RING_PTR ((const uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_ring_start))      /* uncached alias: the DMA writes behind the cache */
#define RING_OFF ((uint32_t)(uintptr_t)&_ring_start)
#define RING_SZ  0x6000u
#define FRAME    1024u
static const uint32_t rates[4] = { 0u, 40000u, 176000u, 440000u };      /* 0 = as fast as the data arrives */

lp_t lp; stream_t lp_stream; static queue_t q; static uint32_t credit_cyc, last_cyc, due_x;
static uint32_t fnv_hist_id[8], fnv_hist_h[8], fnv_n; static uint32_t fnv;

void listplay_init(void)
{
    stream_init(&lp_stream, RING_SZ, 5u * CLK_HZ);
    library_load();
    lp.rate_sel = 0; lp.rate_bps = rates[0];
    if (lib_state != 1) return;
    uint16_t *ids = library_queue_buf();
    uint16_t *orig = library_orig_buf();
    for (uint32_t i = 0; i < lib_n_tracks && i < 16384u; i++) ids[i] = orig[i] = (uint16_t)i;
    queue_set(&q, ids, lib_n_tracks); lp.qn = q.n;
}

static void begin_track(void)
{
    lp.cur_id = queue_cur(&q); lp.consumed = 0; fnv = 2166136261u; due_x = 0;
    char path[LIBP_MAX_PATH]; uint32_t n = library_path(lp.cur_id, path, sizeof path);
    lp.expect = library_size(lp.cur_id);
    stream_close(&lp_stream);                                           /* invalidates anything in flight; wait it out before the one command channel is reused */
    uint32_t t0 = cycles();
    while (lp_stream.fl.active) {
        io_result_t r = slot_read_poll();
        io_req_t rq; stream_pump(&lp_stream, cycles(), r, &rq);
        if ((uint32_t)(cycles() - t0) > 6u * CLK_HZ) break;
    }
    lp.state = 2;
    if (!n || !slot_open_path(SLOT_MEDIA, path)) { lp.open_err++; lp.skipped++; lp.state = 0; return; }
    stream_open(&lp_stream, 0, lp.expect, 0);
    lp.state = 1; last_cyc = cycles();
}

static void play_from_queue(uint16_t id_ok)
{
    for (uint32_t guard = 0; id_ok != Q_NONE && guard < 4u; guard++) {
        begin_track();
        if (lp.state == 1) return;
        id_ok = queue_next(&q, 1);                                      /* a track that would not open is skipped */
    }
    lp.state = 0;
}

void listplay_key(uint32_t edge)
{
    if (lib_state != 1) return;
    if (edge & KEY_A) { lp.t0_cyc = cycles(); lp.bytes_total = 0; play_from_queue(queue_cur(&q)); }
    if (edge & KEY_B) { stream_close(&lp_stream); lp.state = 0; }
    if (edge & KEY_RIGHT) play_from_queue(queue_next(&q, 0));
    if (edge & KEY_LEFT)  play_from_queue(queue_prev(&q));
    if (edge & KEY_X) {
        if (lp.shuffle) queue_restore(&q, library_orig_buf(), lib_n_tracks);
        else queue_shuffle(&q, cycles() | 1u, 1);
        lp.shuffle ^= 1u;
    }
    if (edge & KEY_Y) { lp.rate_sel = (lp.rate_sel + 1u) & 3u; lp.rate_bps = rates[lp.rate_sel]; }      /* flat out, 40, 176, 440 KB/s */
}

void listplay_pump(void)
{
    stream_t *s = &lp_stream;
    io_req_t rq;
    io_result_t r = s->fl.active ? slot_read_poll() : IO_PENDING;
    if (stream_pump(s, cycles(), r, &rq)) slot_read_start(SLOT_MEDIA, rq.off, RING_OFF + rq.dst, rq.len);
    lp.qn = q.n; lp.cur_pos = q.pos; lp.repeat = q.repeat;
    if (lp.state != 1) return;
    uint32_t now = cycles(), dt = now - last_cyc; last_cyc = now;
    uint32_t want;
    if (lp.rate_bps == 0) want = FRAME;
    else {                                                              /* credit in byte-microseconds so no divide runs per pass */
        credit_cyc += dt; if (credit_cyc > CLK_HZ / 5u) credit_cyc = CLK_HZ / 5u;
        uint32_t per_frame = (uint32_t)(((uint64_t)FRAME * CLK_HZ) / lp.rate_bps);
        want = credit_cyc >= per_frame ? FRAME : 0u;
        if (want) credit_cyc -= per_frame;
    }
    for (uint32_t rep = 0; want && rep < (lp.rate_bps ? 1u : 4u); rep++) {      /* flat out: up to four frames a pass; the stack buffer stays small */
        uint32_t n = want;
        if (s->eof && s->avail && s->avail < n) n = s->avail;
        if (stream_ready(s, n)) {
            uint8_t tmp[FRAME];
            stream_take(s, RING_PTR, tmp, n);
            for (uint32_t i = 0; i < n; i++) { fnv ^= tmp[i]; fnv *= 16777619u; }
            lp.consumed += n; lp.consumed_total += n; lp.bytes_total += n;
        } else break;                                                   /* starved: the stream counts it */
    }
    if (s->eof && s->avail == 0u) {                                     /* the track is complete */
        lp.tracks_done++;
        if (lp.expect && lp.consumed != lp.expect) lp.size_bad++; else lp.tracks_ok++;
        lp.hash_last = fnv; uint32_t k = 0;
        for (; k < 8u && k < fnv_n; k++) if (fnv_hist_id[k] == lp.cur_id) { if (fnv_hist_h[k] != fnv) lp.hash_bad++; else lp.hash_seen++; break; }
        if (k >= 8u || k >= fnv_n) { uint32_t w = fnv_n++ & 7u; fnv_hist_id[w] = lp.cur_id; fnv_hist_h[w] = fnv; }
        uint32_t ms = (cycles() - lp.t0_cyc) / (CLK_HZ / 1000u);          /* the 32-bit cycle counter wraps at 64 s: fine for this rate, a run longer than that reads low */
        lp.kbps = ms ? lp.bytes_total / ms : 0u;                           /* bytes per ms = KB/s */
        uint16_t nx = queue_next(&q, 1);
        if (nx == Q_NONE) lp.state = 0; else play_from_queue(nx);
    }
}
