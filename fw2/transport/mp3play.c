#include "mp3play.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../stream/slot.h"
#include "../stream/library.h"
#include "../cores/mp3_frames_core.h"

extern char _ring_start;
#define RING_PTR ((const uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_ring_start))
#define RING_OFF ((uint32_t)(uintptr_t)&_ring_start)
#define RING_SZ  0x6000u
enum { MPS_IDLE = 0, MPS_DEC = 1, MPS_ENDDRAIN = 2, MPS_WAITRATE = 3 };

mp_t mp; tl_t mp_tl; gov_t mp_gov;
lp_t lp; stream_t lp_stream;
static queue_t q; static mp3d_t dec; static int cymo_present_; static uint32_t cur_rate;
static int seg_open_;

void mp3play_init(int cymo_present)
{
    cymo_present_ = cymo_present;
    stream_init(&lp_stream, RING_SZ, 5u * CLK_HZ);
    library_load();
    aout_gain_unity();
    aout_snap_t s = aout_snap(); tl_init(&mp_tl, s.cnt); gov_init(&mp_gov, AOUT_USABLE);
    if (lib_state != 1) return;
    uint16_t *ids = library_queue_buf(), *orig = library_orig_buf();
    for (uint32_t i = 0; i < lib_n_tracks && i < 16384u; i++) ids[i] = orig[i] = (uint16_t)i;
    queue_set(&q, ids, lib_n_tracks); lp.qn = q.n;
}

static int begin_track(void)
{
    lp.cur_id = queue_cur(&q); lp.expect = library_size(lp.cur_id);
    char path[LIBP_MAX_PATH]; uint32_t n = library_path(lp.cur_id, path, sizeof path);
    stream_close(&lp_stream);
    uint32_t t0 = cycles();
    while (lp_stream.fl.active) {                                       /* the one command channel: wait the in-flight read out */
        io_result_t r = slot_read_poll(); io_req_t rq; stream_pump(&lp_stream, cycles(), r, &rq);
        if ((uint32_t)(cycles() - t0) > 6u * CLK_HZ) break;
    }
    if (!n || !slot_open_path(SLOT_MEDIA, path)) { lp.open_err++; lp.skipped++; return 0; }
    stream_open(&lp_stream, 0, lp.expect, 0);
    mp3d_open(&dec, MP3InitDecoder());
    seg_open_ = 0; mp.gen++; mp.state = MPS_DEC; mp.playing = 1; lp.state = 1;
    return 1;
}

static void start_from(uint16_t id)
{
    for (uint32_t guard = 0; id != Q_NONE && guard < 4u; guard++) {
        if (begin_track()) return;
        id = queue_next(&q, 1);
    }
    mp.state = MPS_IDLE; mp.playing = 0; lp.state = 0;
}

static void flush_and_start(uint16_t id)
{
    aout_flush(); aout_snap_t s = aout_snap(); tl_flush(&mp_tl, s.cnt); seg_open_ = 0;
    mp.held = 0; aout_hold(0);
    start_from(id);
}

void mp3play_key(uint32_t edge)
{
    if (lib_state != 1) return;
    if (edge & KEY_A) flush_and_start(queue_cur(&q));
    if (edge & KEY_B) { mp.held = !mp.held; aout_hold((int)mp.held); }
    if (edge & KEY_RIGHT) flush_and_start(queue_next(&q, 0));
    if (edge & KEY_LEFT)  flush_and_start(queue_prev(&q));
    if (edge & KEY_X) {
        if (lp.shuffle) queue_restore(&q, library_orig_buf(), lib_n_tracks); else queue_shuffle(&q, cycles() | 1u, 1);
        lp.shuffle ^= 1u;
    }
}

static void track_ended(void)
{
    if (seg_open_) { if (tl_close(&mp_tl)) mp.acct_bad++; seg_open_ = 0; }
    mp.ended++;
    MP3FreeDecoder(dec.dec);
    uint16_t nx = queue_next(&q, 1);
    if (nx == Q_NONE) { mp.state = MPS_ENDDRAIN; return; }
    /* the next track's rate comes from the index details (no card access): the same rate continues without a flush; anything else drains first */
    uint32_t nrate = library_rate(nx);
    if (nrate && nrate == cur_rate) { mp.gapless_next++; start_from(nx); }
    else { mp.drain_next++; mp.state = MPS_WAITRATE; }
}

void mp3play_pump(void)
{
    stream_t *s = &lp_stream;
    io_req_t rq; io_result_t r = s->fl.active ? slot_read_poll() : IO_PENDING;
    if (stream_pump(s, cycles(), r, &rq)) slot_read_start(SLOT_MEDIA, rq.off, RING_OFF + rq.dst, rq.len);
    lp.qn = q.n; lp.cur_pos = q.pos;
    if (mp.state == MPS_WAITRATE || mp.state == MPS_ENDDRAIN) {           /* everything put must be heard before the rate may change or the list may end */
        aout_snap_t sn = aout_snap();
        if ((int32_t)(aout_heard(&sn) - mp_tl.put_total) >= 0) {
            if (mp.state == MPS_ENDDRAIN) { mp.state = MPS_IDLE; mp.playing = 0; lp.state = 0; }
            else { aout_flush(); aout_snap_t s2 = aout_snap(); tl_flush(&mp_tl, s2.cnt); start_from(queue_cur(&q)); }
        }
        return;
    }
    if (mp.state != MPS_DEC) return;
    for (uint32_t k = 0; k < 2u; k++) {
        aout_snap_t sn = aout_snap(); uint32_t fill = aout_fill(&sn);
        mp.gov_mode = (uint32_t)gov_update(&mp_gov, fill);
        if (!gov_decode_wanted(&mp_gov, fill)) break;
        if (aout_room() < 1152u) { mp.stalls++; break; }
        pcm_blk_t b; uint32_t t0 = cycles();
        mp3d_step_t st = mp3d_step(&dec, s, RING_PTR, &b);
        uint32_t dt = cycles() - t0; if (dt > mp.dec_max_cyc) mp.dec_max_cyc = dt; mp.dec_steps++;
        mp.frames = dec.frame_idx; mp.errors = dec.errors; mp.resyncs = dec.resyncs; mp.pairs_out = dec.pairs_out; mp.pairs_total = dec.pairs_total; mp.ch = dec.ch;
        if (st == MS_BLOCK) {
            if (!seg_open_) {
                if (!cur_rate || dec.rate != cur_rate) {                 /* a rate we are not at: only legal on an empty ring (flushed or drained) */
                    aout_set_rate(dec.rate, cymo_present_); cur_rate = dec.rate; mp.rate = dec.rate; mp.rate_changes++;
                }
                tl_seg_t *g = tl_open(&mp_tl, mp.gen, b.src_pos, dec.pairs_total, 0u, 1u, 1u, 0u);
                if (!g) break;
                seg_open_ = 1;
            }
            uint32_t acc = aout_put(b.pcm, b.pairs, b.ch == 2u);
            tl_put(&mp_tl, acc); mp.seg_put += acc;
            if (acc < b.pairs) mp.stalls++;
        } else if (st == MS_EOF) { track_ended(); break; }
        else if (st == MS_ERROR) { mp.state = MPS_IDLE; break; }
        else { mp.need_data++; break; }
    }
}

void mp3play_heard(void)
{
    aout_snap_t s = aout_snap(); uint32_t h = aout_heard(&s);
    tl_advance(&mp_tl, h);
    const tl_seg_t *g = tl_current(&mp_tl, h);
    mp.cur_valid = g != 0;
    if (g) { mp.cur_gen = g->gen; mp.cur_pos = tl_pos(g, h); }
}
