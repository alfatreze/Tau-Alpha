#include "mp3play.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../stream/slot.h"
#include "../stream/library.h"
#include "../cores/mp3_frames_core.h"
#include "../cores/gain_core.h"
/* Tempo (spec 8.3): the Tau-written stretcher and funnel are reused unchanged. Staging ring: the PSRAM window at +6 MiB, planar (the library image, queue and dead-track bitmap are below it). */
#define TEMPO_PS_L ((volatile uint32_t *)(uintptr_t)0xA4600000u)
#define TEMPO_PS_R ((volatile uint32_t *)(uintptr_t)0xA4610000u)
#define TEMPO_OUT_CAP 2800u                               /* output pairs one decoded block can yield: 1152 / 0.5 = 2304 at the slowest speed, plus the hop held back */
static int16_t tempo_buf[2u * TEMPO_OUT_CAP]; static uint32_t tempo_n, tempo_over;
#define TEMPO_PUSH(l, r) tempo_push((l), (r))
static inline int tempo_push(int16_t l, int16_t r) { if (tempo_n < TEMPO_OUT_CAP) { tempo_buf[2u * tempo_n] = l; tempo_buf[2u * tempo_n + 1u] = r; tempo_n++; } else tempo_over++; return 1; }
#define WS_DATA
#include "../../fw/tempo_core.h"
static tempo_t tempo_st; static uint8_t tempo_active;
static const uint16_t tempo_q8[8] = { 192u, 218u, 256u, 282u, 320u, 384u, 448u, 512u };      /* 0.75 0.85 1.00 1.10 1.25 1.50 1.75 2.00 */

extern char _ring_start;
#define RING_PTR ((const uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_ring_start))
#define RING_OFF ((uint32_t)(uintptr_t)&_ring_start)
#define RING_SZ  0x6000u
enum { MPS_IDLE = 0, MPS_DEC = 1, MPS_ENDDRAIN = 2, MPS_WAITRATE = 3 };

mp_t mp; tl_t mp_tl; gov_t mp_gov;
lp_t lp; stream_t lp_stream;
static queue_t q; static mp3d_t dec; static int cymo_present_; static uint32_t cur_rate;
static int seg_open_; static uint8_t fade_pending;
/* The gain of each segment (volume x ReplayGain) is applied when the segment is HEARD; the table is keyed by the segment's generation. */
static struct { uint32_t gen; gain_trk_t g; } seg_gain[4]; static gain_trk_t dec_g, heard_g = { 32768u, 32768u }; static uint32_t heard_gain_gen;
static void gain_apply_heard(void) { mp.gain_tgt = gain_target(mp.volume, &heard_g); mp.rg_factor = heard_g.factor; mp.rg_cap = heard_g.cap; aout_gain_set(mp.gain_tgt); }
static void rg_for_track(uint32_t id)
{
    uint32_t fl = 0, tp = 0, ap = 0; int32_t tg = 0, ag = 0;
    if (!library_rg(id, &fl, &tg, &ag, &tp, &ap)) fl = 0;
    dec_g = gain_track(mp.rg_mode, fl, tg, ag, tp, ap, (REG_RD(R_GAIN_CTRL) >> 30) & 1u);
}

#if TAU_POLY_FW
#include "../../fw/mp3_poly_hw.h"
#endif
void mp3play_init(int cymo_present)
{
#if TAU_POLY_FW
    tau_poly_hw_enable = caps.poly ? 1 : 0;                            /* the window unit is used when the bitstream has it; any anomaly turns it off for the session */
#endif
    cymo_present_ = cymo_present;
    stream_init(&lp_stream, RING_SZ, 5u * CLK_HZ);
    library_load();
    aout_gain_unity();
    mp.volume = 94u; mp.rg_mode = RG_TRACK; mp.tempo_idx = 2u;                              /* the old player's default volume; ReplayGain on Track for the first hardware listening */
    gain_apply_heard(); REG_WR(R_GAIN_CTRL, 1u | (3u << 4) | 2u);       /* snap to the volume target: no ramp from a stale value */
    aout_snap_t s = aout_snap(); tl_init(&mp_tl, s.cnt); gov_init(&mp_gov, AOUT_USABLE);
    if (lib_state != 1) return;
    uint16_t *ids = library_queue_buf(), *orig = library_orig_buf();
    for (uint32_t i = 0; i < lib_n_tracks && i < 16384u; i++) ids[i] = orig[i] = (uint16_t)i;
    queue_set(&q, ids, lib_n_tracks); lp.qn = q.n;
}

static int begin_track(void)
{
    lp.cur_id = queue_cur(&q); lp.expect = library_size(lp.cur_id); rg_for_track(lp.cur_id);
    char path[LIBP_MAX_PATH]; uint32_t n = library_path(lp.cur_id, path, sizeof path);
    stream_close(&lp_stream);
    uint32_t t0 = cycles();
    while (lp_stream.fl.active) {                                       /* the one command channel: wait the in-flight read out */
        io_result_t r = slot_read_poll(); io_req_t rq; stream_pump(&lp_stream, cycles(), r, &rq);
        if ((uint32_t)(cycles() - t0) > 6u * CLK_HZ) break;
    }
    if (!n || !slot_open_path(SLOT_MEDIA, path)) { lp.open_err++; lp.skipped++; return 0; }
    stream_open(&lp_stream, 0, lp.expect, 0);
    if (dec.dec) { MP3FreeDecoder(dec.dec); dec.dec = 0; }              /* the 24 KB arena holds exactly one decoder: a skip must free the old one first */
    if (!mp3d_open(&dec, MP3InitDecoder())) { mp.init_fail++; return 0; }
    dec.file_size = lp.expect;                                          /* for the seek plan (the library knows the size) */
    seg_open_ = 0; tempo_active = 0; mp.gen++; mp.state = MPS_DEC; mp.playing = 1; lp.state = 1;
    return 1;
}

static void start_from(uint16_t id)
{
    for (uint32_t guard = 0; id != Q_NONE && guard < 8u; guard++) {
        if (library_codec(id) != 1u) { mp.non_mp3++; id = queue_next(&q, 1); continue; }      /* FLAC waits for S8 */
        if (begin_track()) return;
        id = queue_next(&q, 1);
    }
    mp.state = MPS_IDLE; mp.playing = 0; lp.state = 0;
}

static void flush_and_start(uint16_t id)
{
    if (mp.playing && !mp.held) aout_mute_wait();                       /* the old tail fades out through the gain stage, then is dropped unheard */
    aout_flush(); aout_snap_t s = aout_snap(); tl_flush(&mp_tl, s.cnt); seg_open_ = 0;
    mp.held = 0; aout_hold(0); fade_pending = 1;                         /* the new audio fades in from silence at its first put */
    start_from(id);
}

/* +secs from where it is heard (wrapping to the start past the end). Only while the heard track is the one being decoded; across a gapless boundary the request is ignored and counted. */
static void do_seek(uint32_t secs)
{
    if (mp.state != MPS_DEC || !mp.cur_valid || mp.cur_gen != mp.gen || !dec.dec || !mp.rate) { mp.seek_ign++; return; }
    uint32_t target = mp.cur_pos + secs * mp.rate;
    if (dec.pairs_total && target + mp.rate >= dec.pairs_total) target = 0;
    mp3_seek_t pl;
    if (!mp3d_plan_seek(&dec, target, &pl)) { mp.seek_fail++; return; }
    if (!mp.held) aout_mute_wait();
    aout_flush(); aout_snap_t s = aout_snap(); tl_flush(&mp_tl, s.cnt); seg_open_ = 0; tempo_active = 0;
    if (!mp3d_seek(&dec, &lp_stream, &pl)) { mp.seek_fail++; mp.state = MPS_IDLE; return; }
    mp.held = 0; aout_hold(0); fade_pending = 1; mp.seeks++; mp.seek_to = target;
}

/* A new speed: restart from where it is heard (a seek to the same pair rebuilds the stretcher; later tracks start at the new speed). */
static void tempo_step(int dir)
{
    int i = (int)mp.tempo_idx + dir; if (i < 0 || i > 7) return;
    mp.tempo_idx = (uint32_t)i;
    if (mp.state == MPS_DEC && mp.cur_valid && mp.cur_gen == mp.gen) do_seek(0);
}

void mp3play_key(uint32_t edge)
{
    if (lib_state != 1) return;
    if (edge & KEY_A) flush_and_start(queue_cur(&q));
    if (edge & KEY_B) {
        if (!mp.held) { aout_mute_wait(); mp.held = 1; aout_hold(1); }
        else { mp.held = 0; aout_hold(0); aout_fade_in(mp.gain_tgt); }
    }
    if (edge & KEY_RIGHT) flush_and_start(queue_next(&q, 0));
    if (edge & KEY_LEFT)  flush_and_start(queue_prev(&q));
    if ((edge & KEY_UP) || (edge & KEY_DOWN)) {                          /* previous / next album */
        uint32_t first = library_album_jump(queue_cur(&q), (edge & KEY_DOWN) ? 1 : -1);
        if (first != 0xFFFFFFFFu && queue_goto(&q, (uint16_t)first)) flush_and_start((uint16_t)first);
    }
    if (REG_RD(R_INPUT) & KEY_SELECT) {                                  /* Select is a modifier: +L1/R1 tempo slower/faster, +Y ReplayGain mode */
        if (edge & KEY_R1) tempo_step(1);
        if (edge & KEY_L1) tempo_step(-1);
        if (edge & KEY_Y) { mp.rg_mode = (mp.rg_mode + 1u) % 3u; rg_for_track(lp.cur_id); heard_g = dec_g; gain_apply_heard(); }
        return;
    }
    if (edge & KEY_Y) do_seek(30);
    if (edge & KEY_L1) { mp.volume = mp.volume >= 3u ? mp.volume - 3u : 0u; gain_apply_heard(); }
    if (edge & KEY_R1) { mp.volume = mp.volume + 3u > 100u ? 100u : mp.volume + 3u; gain_apply_heard(); }

    if (edge & KEY_X) {
        if (lp.shuffle) queue_restore(&q, library_orig_buf(), lib_n_tracks); else queue_shuffle(&q, cycles() | 1u, 1);
        lp.shuffle ^= 1u;
    }
}

static void track_ended(void)
{
    if (seg_open_) { if (tl_close(&mp_tl)) mp.acct_bad++; seg_open_ = 0; }
    mp.ended++;
    MP3FreeDecoder(dec.dec); dec.dec = 0;
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
        if (aout_room() < (tempo_q8[mp.tempo_idx] == 256u ? 1152u : TEMPO_OUT_CAP)) { mp.stalls++; break; }      /* the whole output of one decoded block must fit before it is decoded */
        pcm_blk_t b; uint32_t t0 = cycles();
        mp3d_step_t st = mp3d_step(&dec, s, RING_PTR, &b);
        uint32_t dt = cycles() - t0; if (dt > mp.dec_max_cyc) mp.dec_max_cyc = dt; mp.dec_steps++;
        if (st == MS_BLOCK) { mp.dec_us += dt / (CLK_HZ / 1000000u); mp.dec_blocks++; mp.dec_avg_us = mp.dec_us / mp.dec_blocks; }
        mp.frames = dec.frame_idx; mp.errors = dec.errors; mp.resyncs = dec.resyncs; mp.pairs_out = dec.pairs_out; mp.pairs_total = dec.pairs_total; mp.ch = dec.ch;
        if (st == MS_BLOCK) {
            const uint32_t q8 = tempo_q8[mp.tempo_idx], neutral = q8 == 256u;
            if (!neutral && !tempo_active) { tempo_start(&tempo_st, dec.rate, (uint8_t)b.ch, q8); tempo_active = 1; }      /* (re)start the stretcher at the first block of a segment */
            if (!seg_open_) {
                if (!cur_rate || dec.rate != cur_rate) {                 /* a rate we are not at: only legal on an empty ring (flushed or drained) */
                    aout_set_rate(dec.rate, cymo_present_); cur_rate = dec.rate; mp.rate = dec.rate; mp.rate_changes++;
                }
                uint32_t rest = dec.pairs_total > b.src_pos ? dec.pairs_total - b.src_pos : 0u;
                tl_seg_t *g = tl_open(&mp_tl, mp.gen, b.src_pos, neutral ? rest : (uint32_t)(((uint64_t)rest * 256u) / q8), neutral ? 0u : 4096u, neutral ? 1u : 256u, neutral ? 1u : (uint16_t)q8, neutral ? 0u : TLF_TEMPO);
                if (!g) break;
                seg_open_ = 1; seg_gain[g->gen & 3u].gen = g->gen; seg_gain[g->gen & 3u].g = dec_g;
            }
            if (fade_pending) { fade_pending = 0; heard_g = dec_g; heard_gain_gen = mp.gen; gain_apply_heard(); aout_fade_in(mp.gain_tgt); }
            uint32_t acc;
            if (neutral) acc = aout_put(b.pcm, b.pairs, b.ch == 2u);
            else {
                tempo_n = 0; tempo_frame(&tempo_st, b.pcm, b.pairs * b.ch, b.ch == 2u);
                acc = tempo_n ? aout_put(tempo_buf, tempo_n, 1) : 0u; mp.tempo_out = tempo_n;
                if (acc < tempo_n) mp.stalls++;
            }
            tl_put(&mp_tl, acc); mp.seg_put += acc;
            if (neutral && acc < b.pairs) mp.stalls++;
        } else if (st == MS_EOF) { track_ended(); break; }
        else if (st == MS_ERROR) { mp.state = MPS_IDLE; break; }
        else { mp.need_data++; break; }
    }
}

void mp3play_heard(void)
{
#if TAU_POLY_FW
    mp.poly_on = (uint32_t)tau_poly_hw_enable; mp.poly_slots = tau_poly_stat_slots; mp.poly_mismatch = tau_poly_stat_mismatch; mp.poly_timeout = tau_poly_stat_timeout;
#endif
    aout_snap_t s = aout_snap(); uint32_t h = aout_heard(&s);
    tl_advance(&mp_tl, h);
    const tl_seg_t *g = tl_current(&mp_tl, h);
    mp.cur_valid = g != 0;
    if (g) {
        mp.cur_gen = g->gen; mp.cur_pos = tl_pos(g, h);
        if (g->gen != heard_gain_gen && seg_gain[g->gen & 3u].gen == g->gen) {      /* SEG_ENTER: the track now heard brings its own ReplayGain; the stage ramps over 5 ms */
            heard_gain_gen = g->gen; heard_g = seg_gain[g->gen & 3u].g; gain_apply_heard();
        }
    }
}
