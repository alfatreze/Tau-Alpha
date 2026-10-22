#include "mp3play.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../aout/halcyon.h"
#include "../stream/slot.h"
#include "../stream/library.h"
#include "../cores/mp3_frames_core.h"
#include "../cores/gain_core.h"
#include "../cores/nowplaying_core.h"
#include "../cores/lastplayed_core.h"
#include "../stream/skiplist.h"
#if P2_FLAC2
#include "../decode/flac2_unit.h"
#if TAU_LPC_FW
#include "../../fw/flac_lpc_hw.h"
#endif
#include <stdlib.h>
#ifndef FLAC_CHUNK
#define FLAC_CHUNK 8192u
#endif
#define FLAC_CH0_WORDS 5120u                              /* 20 KB from the decoder arena (shared with Helix: one codec is live at a time); a file with larger blocks is refused */
#define F2_OUT_PAIRS 64u                                  /* pairs per decoder call (512 B of int32) */
#define F2_PASS_PAIRS 1024u                               /* pairs decoded per pump pass: about 6 ms of decode, inside the 8 ms atomic-step budget (spec 8) */
#define F2_RES_BUDGET 512u                                /* residuals per call */
/* The FLAC decoder's state lives ON TOP OF the MP3 decoder's (`DEC`, 6.3 KB): only one codec is live at a time, and the 192 KB link has no room for both (spec 4 of the review). dec_live says whether `DEC` holds a
 * live MP3 decoder; a FLAC track overwrites it, an MP3 track re-initialises it. */
static union { mp3d_t m; struct { f2t_t t; uint8_t seek[1024]; int32_t out[2u * F2_OUT_PAIRS]; int16_t pcm[2u * F2_OUT_PAIRS]; } f; } dv;
static int32_t *flac_ch0; static uint8_t is_flac, ft_active, dec_live; static uint32_t ft_total, ft_rate;
static uint32_t ft_cyc;                                   /* decode cycles since the last verified frame (for the per-frame average) */
#endif
/* Tempo (spec 8.3): the Tau-written stretcher and funnel are reused unchanged. Staging ring: the PSRAM window at +6 MiB, planar (the library image, queue and dead-track bitmap are below it). */
#define TEMPO_PS_L ((volatile uint32_t *)(uintptr_t)0xA4600000u)
#define TEMPO_PS_R ((volatile uint32_t *)(uintptr_t)0xA4610000u)
#define TEMPO_OUT_CAP 2100u                               /* output pairs one decoded block can yield: 1152 / 0.75 = 1536 at the slowest offered speed, plus a hop (512) */
#define TEMPO_SLICE 1                                     /* the RAM diet's slice mode (host-equivalent, B-674): about 1.75 KB less on-chip RAM */
/* The stretched output goes to the ring in blocks of 128 pairs as it is produced; the room for the whole output of a decoded block is checked BEFORE decoding it, so no block is ever refused. */
#define TEMPO_BLK 128u
static int16_t tempo_buf[2u * TEMPO_BLK]; static uint32_t tempo_n, tempo_total, tempo_acc, tempo_over;
static void tempo_flush(void);
#define TEMPO_PUSH(l, r) tempo_push((l), (r))
static inline int tempo_push(int16_t l, int16_t r) { tempo_buf[2u * tempo_n] = l; tempo_buf[2u * tempo_n + 1u] = r; tempo_n++; tempo_total++; if (tempo_n == TEMPO_BLK) tempo_flush(); return 1; }
#define WS_DATA
#include "../../fw/tempo_core.h"
static tempo_t tempo_st; static uint8_t tempo_active;
#if P2_FLAC2
#define DEC (dv.m)
#define ft (dv.f.t)
#define ft_seek_buf (dv.f.seek)
#define fl_out (dv.f.out)
#define fl_pcm (dv.f.pcm)
#endif
static void tempo_flush(void)
{
    if (!tempo_n) return;
    uint32_t acc = aout_put(tempo_buf, tempo_n, 1); tl_put(&mp_tl, acc); mp.seg_put += acc; tempo_acc += acc;
    if (acc < tempo_n) { mp.stalls++; tempo_over += tempo_n - acc; }
    tempo_n = 0;
}
static const uint16_t tempo_q8[8] = { 192u, 218u, 256u, 282u, 320u, 384u, 448u, 512u };      /* 0.75 0.85 1.00 1.10 1.25 1.50 1.75 2.00 (the menu offers index 2 = OFF and 3..7) */
/* Speed (varispeed, the pitch follows): the output tick times num/den. The menu list is 0.85 0.95 1.00 1.10 1.20 1.30 1.50 1.75 2.00 (index 2 = normal). MP3 only: a FLAC track always plays at 1.00x. */
static const uint8_t speed_num[9] = { 17u, 19u, 1u, 11u, 6u, 13u, 3u, 7u, 2u }, speed_den[9] = { 20u, 20u, 1u, 10u, 5u, 10u, 2u, 4u, 1u };
static void speed_apply(void);
static uint8_t tempo_pend, tempo_tries;

#define SMALL __attribute__((optimize("Os")))      /* not on the decode path: smaller code is what the 192 KB link needs */
extern char _ring_start;
#define RING_PTR ((const uint8_t *)(uintptr_t)(0xC0000000u + (uint32_t)(uintptr_t)&_ring_start))
#define RING_OFF ((uint32_t)(uintptr_t)&_ring_start)
#define RING_SZ  0x6000u
enum { MPS_IDLE = 0, MPS_DEC = 1, MPS_ENDDRAIN = 2, MPS_WAITRATE = 3 };

mp_t mp; tl_t mp_tl; gov_t mp_gov;
lp_t lp; stream_t lp_stream;
uint8_t mp3play_accept_all;                                      /* Settings > Diagnostics > Accept all rates: FLAC above 48 kHz is decoded (plays at 48 kHz speed: a measurement aid, not a feature) */
static queue_t q;
#if !P2_FLAC2
static mp3d_t dec;
#define DEC dec
#define dec_live 1
#endif
static int cymo_present_; static uint32_t cur_rate;
static int seg_open_; static uint8_t fade_pending;
static uint32_t skip_lead_p, skip_tail_p; static uint8_t skip_pending;       /* skip silence: the numbers for the track being opened, applied at its first decoded block */
static uint32_t qsrc_kind = LQ_ALL_NATURAL, qsrc_parent;      /* how the current queue was made (to rebuild its natural order)*/
static np_info_t np; static void np_refresh(const tl_seg_t *g, uint32_t id);
uint32_t last_state, last_boot[3];                                  /* Info: how the restore went (0 not tried, 1 restored, 2 no record, 3 check failed, 4 the track is not in that queue, 5 no library) and the three words read at boot */
static uint32_t lastw[3];                                         /* the last played track as persisted words (cores/lastplayed_core.h) */
static void last_note(uint32_t id) { lastp_t l = { 1u, qsrc_kind, qsrc_parent, id }; lastp_pack(&l, lastw); }
/* The gain of each segment (volume x ReplayGain) is applied when the segment is HEARD; the table is keyed by the segment's generation. */
static struct { uint32_t gen; gain_trk_t g; uint32_t id; } seg_gain[4]; static gain_trk_t dec_g, heard_g = { 32768u, 32768u }; static uint32_t heard_gain_gen;
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
SMALL void mp3play_init(int cymo_present)
{
#if TAU_POLY_FW
    tau_poly_hw_enable = caps.poly ? 1 : 0;
#endif
#if P2_FLAC && TAU_LPC_FW
    tau_lpc_hw_enable = caps.lpc ? 1 : 0;                            /* the window unit is used when the bitstream has it; any anomaly turns it off for the session */
#endif
    cymo_present_ = cymo_present;
    stream_init(&lp_stream, RING_SZ, 5u * CLK_HZ);
    library_load();
    aout_gain_unity();
    mp.volume = 94u; mp.rg_mode = RG_TRACK; mp.tempo_idx = 2u; mp.speed_idx = 2u; mp.sel_pitch = 2u; mp.sel_stretch = 0u; mp.seek_idx = 1u;                              /* the old player's default volume; ReplayGain on Track for the first hardware listening */
    gain_apply_heard(); REG_WR(R_GAIN_CTRL, 1u | (3u << 4) | 2u);       /* snap to the volume target: no ramp from a stale value */
    aout_snap_t s = aout_snap(); tl_init(&mp_tl, s.cnt); gov_init(&mp_gov, AOUT_USABLE);
    if (lib_state != 1) return;
    uint16_t *ids = library_queue_buf(), *orig = library_orig_buf();
    for (uint32_t i = 0; i < lib_n_tracks && i < 16384u; i++) ids[i] = orig[i] = (uint16_t)i;
    queue_set(&q, ids, lib_n_tracks); lp.qn = q.n;
}

static void io_service(void)
{
    stream_t *s = &lp_stream; io_req_t rq; io_result_t r = s->fl.active ? slot_read_poll() : IO_PENDING;
    if (stream_pump(s, cycles(), r, &rq)) slot_read_start(SLOT_MEDIA, rq.off, RING_OFF + rq.dst, rq.len);
}

#if P2_FLAC2
static void flac_release(void) { if (flac_ch0) { free(flac_ch0); flac_ch0 = 0; } ft_active = 0; }
SMALL static int flac_begin(void)
{
    flac_release();
    flac_ch0 = (int32_t *)malloc(FLAC_CH0_WORDS * 4u);
    if (!flac_ch0) { mp.init_fail++; return 0; }
    flac2_init(&ft, ft_seek_buf, sizeof ft_seek_buf, flac_ch0, FLAC_CH0_WORDS);
    lp_stream.chunk = FLAC_CHUNK;                                    /* a 24-bit stream needs about 200 KB/s: fewer, larger card reads */
    flac2_begin(&ft, &lp_stream, lp.expect);                         /* (re)opens the stream at byte 0; the metadata is read by the pump */
    ft_active = 1; ft_cyc = 0; ft_total = 0; ft_rate = 0;
    return 1;
}
#endif

SMALL static int begin_track(void)
{
    lp.cur_id = queue_cur(&q); lp.expect = library_size(lp.cur_id); rg_for_track(lp.cur_id); last_note(lp.cur_id);
    char path[LIBP_MAX_PATH]; uint32_t n = library_path(lp.cur_id, path, sizeof path);
    stream_close(&lp_stream);
    uint32_t t0 = cycles();
    while (lp_stream.fl.active) {                                       /* the one command channel: wait the in-flight read out */
        io_result_t r = slot_read_poll(); io_req_t rq; stream_pump(&lp_stream, cycles(), r, &rq);
        if ((uint32_t)(cycles() - t0) > 6u * CLK_HZ) break;
    }
    skip_pending = 0; skip_lead_p = skip_tail_p = 0;
    if (mp.skip_on && n) { uint32_t a = 0, b2 = 0; if (skiplist_lookup(path, &a, &b2)) { skip_lead_p = a; skip_tail_p = b2; skip_pending = 1; } }      /* the card channel is free here */
    if (!n || !slot_open_path(SLOT_MEDIA, path)) { lp.open_err++; lp.skipped++; return 0; }
#if !P2_FLAC2
    stream_open(&lp_stream, 0, lp.expect, 0);
#endif
    if (dec_live && DEC.dec) { MP3FreeDecoder(DEC.dec); DEC.dec = 0; }  /* the 24 KB arena holds exactly one decoder: a skip must free the old one first */
#if P2_FLAC2
    dec_live = 0;
#endif
#if P2_FLAC
    flac_release();
    is_flac = library_codec(lp.cur_id) == 2u;
    speed_apply();
    if (is_flac) { if (!flac_begin()) return 0; }
    else { lp_stream.chunk = 0; stream_open(&lp_stream, 0, lp.expect, 0);
#endif
    {
    if (!mp3d_open(&DEC, MP3InitDecoder())) { mp.init_fail++; return 0; }
#if P2_FLAC2
    dec_live = 1;
#endif
    DEC.file_size = lp.expect;                                          /* for the seek plan (the library knows the size) */
    }
#if P2_FLAC2
    }
#endif
    seg_open_ = 0; tempo_active = 0; mp.gen++; mp.state = MPS_DEC; mp.playing = 1; lp.state = 1;
    return 1;
}

SMALL static void start_from(uint16_t id)
{
    for (uint32_t guard = 0; id != Q_NONE && guard < 8u; guard++) {
        if (library_codec(id) != 1u && !(P2_FLAC && library_codec(id) == 2u)) { mp.non_mp3++; id = queue_next(&q, 1); continue; }
        if (begin_track()) return;
        id = queue_next(&q, 1);
    }
    mp.state = MPS_IDLE; mp.playing = 0; lp.state = 0;
}

SMALL static void flush_and_start(uint16_t id)
{
    if (mp.playing && !mp.held) aout_mute_wait();                       /* the old tail fades out through the gain stage, then is dropped unheard */
    aout_flush(); aout_snap_t s = aout_snap(); tl_flush(&mp_tl, s.cnt); seg_open_ = 0;
    mp.held = 0; aout_hold(0); fade_pending = 1;                         /* the new audio fades in from silence at its first put */
    start_from(id);
}

/* +secs from where it is heard (wrapping to the start past the end). Only while the heard track is the one being decoded; across a gapless boundary the request is ignored and counted. */
/* Where a relative seek lands: forward past the end wraps to the start, backward past the start stops at 0. */
static uint32_t seek_target(int32_t secs, uint32_t total)
{
    uint32_t t;
    if (secs >= 0) { t = mp.cur_pos + (uint32_t)secs * mp.rate; if (total && t + mp.rate >= total) t = 0; }
    else { uint32_t d = (uint32_t)(-secs) * mp.rate; t = d > mp.cur_pos ? 0u : mp.cur_pos - d; }
    return t;
}
SMALL static void do_seek(int32_t secs)
{
#if P2_FLAC2
    if (is_flac && ft_active) {
        if (mp.state != MPS_DEC || !mp.cur_valid || mp.cur_gen != mp.gen || !mp.rate || ft.phase != F2T_PH_PLAY || !ft_total) { mp.seek_ign++; return; }
        uint32_t target = seek_target(secs, ft_total);
        if (!mp.held) aout_mute_wait();
        aout_rollback(); aout_flush(); aout_snap_t s = aout_snap(); tl_flush(&mp_tl, s.cnt); seg_open_ = 0; ft_cyc = 0;
        if (!flac2_seek(&ft, &lp_stream, target)) { mp.seek_fail++; mp.state = MPS_IDLE; return; }
        mp.held = 0; aout_hold(0); fade_pending = 1; mp.seeks++; mp.seek_to = target;
        return;
    }
#endif
    if (mp.state != MPS_DEC || !mp.cur_valid || mp.cur_gen != mp.gen || !dec_live || !DEC.dec || !mp.rate) { mp.seek_ign++; return; }
    uint32_t target = seek_target(secs, DEC.pairs_total);
    mp3_seek_t pl;
    if (!mp3d_plan_seek(&DEC, target, &pl)) { mp.seek_fail++; return; }
    if (!mp.held) aout_mute_wait();
    aout_flush(); aout_snap_t s = aout_snap(); tl_flush(&mp_tl, s.cnt); seg_open_ = 0; tempo_active = 0;
    if (!mp3d_seek(&DEC, &lp_stream, &pl)) { mp.seek_fail++; mp.state = MPS_IDLE; return; }
    mp.held = 0; aout_hold(0); fade_pending = 1; mp.seeks++; mp.seek_to = target;
}

SMALL static void shuffle_set(uint32_t on)
{
    if ((on ? 1u : 0u) == (lp.shuffle ? 1u : 0u) || lib_state != 1) return;
    if (!on) { uint32_t st = 0, n = library_build_queue((uint8_t)qsrc_kind, qsrc_parent, 0, library_orig_buf(), 16384u, &st, 0); if (n) queue_restore(&q, library_orig_buf(), n); }
    else queue_shuffle(&q, cycles() | 1u, 1);
    lp.shuffle = on ? 1u : 0u;
}
SMALL static void speed_apply(void)
{
    uint32_t i = mp.speed_idx > 8u ? 2u : mp.speed_idx;
#if P2_FLAC
    if (is_flac) i = 2u;
#endif
    aout_set_speed(speed_num[i], speed_den[i], cymo_present_);
}
SMALL static void tempo_set(uint32_t i)
{
    if (i > 7u || i == mp.tempo_idx) return;
    mp.tempo_idx = i;
#if P2_FLAC2
    if (is_flac) return;                                                  /* no tempo for FLAC: a restart would only cost a gap */
#endif
    tempo_pend = 1u; tempo_tries = 0u;                                    /* the stretcher restarts from where it is heard: a seek to the same pair, retried by the pump until the seek is accepted */
}
/* Time Stretch: 0 Normal (1.00x), 1 Pitch (varispeed: speed table index through pitch_map), 2 Stretch (pitch-preserving: tempo table index 3 + sel). The two modes exclude each other by construction. */
static const uint8_t pitch_map[8] = { 0u, 1u, 3u, 4u, 5u, 6u, 7u, 8u };      /* 0.85 0.95 1.10 1.20 1.30 1.50 1.75 2.00 */
SMALL static void rate_apply_mode(void)
{
    uint32_t sp = 2u, tp = 2u;
    if (mp.rate_mode == 1u) sp = pitch_map[mp.sel_pitch > 7u ? 0u : mp.sel_pitch];
    else if (mp.rate_mode == 2u) tp = 3u + (mp.sel_stretch > 4u ? 0u : mp.sel_stretch);
    if (sp != mp.speed_idx) { mp.speed_idx = sp; speed_apply(); }
    tempo_set(tp);
}
/* A new speed: restart from where it is heard (a seek to the same pair rebuilds the stretcher; later tracks start at the new speed). */
static void tempo_set(uint32_t i);
SMALL static void tempo_step(int dir)
{
    int i = (int)mp.tempo_idx + dir; if (i < 0 || i > 7) return;
    tempo_set((uint32_t)i);
}


/* The player's commands, called by the input layer (fw2/app/main_player.c) once it has turned presses, taps and holds into intents. */
SMALL void mp3play_act(uint32_t act, int32_t arg)
{
    if (lib_state != 1) return;
    switch (act) {
    case MPA_PLAYPAUSE:
        if (mp.state == MPS_IDLE) { flush_and_start(queue_cur(&q)); break; }
        if (!mp.held) { aout_mute_wait(); mp.held = 1; aout_hold(1); }
        else { mp.held = 0; aout_hold(0); aout_fade_in(mp.gain_tgt); }
        break;
    case MPA_RESTART: flush_and_start(queue_cur(&q)); break;
    case MPA_NEXT: flush_and_start(queue_next(&q, 0)); break;
    case MPA_PREV: flush_and_start(queue_prev(&q)); break;
    case MPA_SEEK: do_seek(arg); break;
    case MPA_VOL: { int v = (int)mp.volume + arg; mp.volume = (uint32_t)(v < 0 ? 0 : v > 100 ? 100 : v); gain_apply_heard(); break; }
    case MPA_ALBUM: {                                                      /* previous / next album */
        uint32_t first = library_album_jump(queue_cur(&q), arg > 0 ? 1 : -1);
        if (first != 0xFFFFFFFFu && queue_goto(&q, (uint16_t)first)) flush_and_start((uint16_t)first);
        break; }
    case MPA_REPEAT: q.repeat = (uint8_t)((q.repeat + 1u) % 3u); lp.repeat = q.repeat; break;
    case MPA_SHUFFLE: shuffle_set(!lp.shuffle); break;
    case MPA_TEMPO: tempo_step(arg); break;
    }
}


static uint8_t bad_run;                                   /* consecutive tracks that produced no verified frame (refused or failed): the list stops after 8 instead of cycling forever */
SMALL static void track_ended(void)
{
    if (seg_open_) { if (tl_close(&mp_tl)) mp.acct_bad++; seg_open_ = 0; }
    mp.ended++;
    if (dec_live && DEC.dec) { MP3FreeDecoder(DEC.dec); DEC.dec = 0; }
#if P2_FLAC2
    dec_live = 0;
#endif
#if P2_FLAC
    flac_release();
#endif
    if (bad_run >= 8u) { bad_run = 0; mp.state = MPS_IDLE; return; }
    uint16_t nx = queue_next(&q, 1);
    if (nx == Q_NONE) { mp.state = MPS_ENDDRAIN; return; }
    /* the next track's rate comes from the index details (no card access): the same rate continues without a flush; anything else drains first */
    uint32_t nrate = library_rate(nx);
    if (nrate && nrate == cur_rate) { mp.gapless_next++; start_from(nx); }
    else { mp.drain_next++; mp.state = MPS_WAITRATE; }
}

#if P2_FLAC2
/* One pump pass of the FLAC decoder (about 1,024 pairs, 6 ms): off the hot path and compiled for size. Returns 1 when the caller's loop should end (no track open). */
SMALL static int flac_pass(stream_t *s)
{
    if (!ft_active) return 1;
    for (uint32_t done = 0; done < F2_PASS_PAIRS; ) {
        if (ft.phase == F2T_PH_PLAY && aout_room() < ft.op.si.max_bs + F2_OUT_PAIRS) { mp.stalls++; break; }      /* a frame in progress holds its staged pairs in the ring: room for the rest of it and one more call */
        if (!seg_open_ && mp_tl.n >= TL_MAX) { mp.stalls++; break; }          /* no free timeline slot for the pairs of a new stretch: wait for the heard side to pop one */
        uint32_t np = 0, t0f = cycles(); int ev = 0;
        int r = flac2_step(&ft, s, RING_PTR, fl_out, F2_OUT_PAIRS, F2_RES_BUDGET, &np, &ev);
        uint32_t dtf = cycles() - t0f; ft_cyc += dtf; if (dtf > mp.dec_max_cyc) mp.dec_max_cyc = dtf; mp.dec_steps++;
        if (r == F2T_NEED) { mp.need_data++; break; }
        if (r == F2T_OPENED) {
            const f2_info_t *si = &ft.op.si;
            if ((si->rate > 48000u && !mp3play_accept_all) || si->channels > 2u || si->bps > 24u || si->max_bs > FLAC_CH0_WORDS) { mp.flac_refused++; bad_run++; track_ended(); break; }
            ft_total = (uint32_t)si->total; ft_rate = si->rate; mp.ch = si->channels; mp.pairs_total = ft_total;
            continue;
        }
        if (r == F2T_END) { track_ended(); break; }
        if (r == F2T_FAIL) { mp.errors++; mp.flac_err = ft.why; if (ft.why == F2T_WHY_NOTFLAC || ft.why == F2T_WHY_UNSUPPORTED) mp.flac_refused++; bad_run++; aout_rollback(); track_ended(); break; }
        if (r != F2T_PAIRS) continue;                                              /* F2T_WORK: metadata, scanning, a seek probe */
        const uint32_t bps = ft.op.si.bps;
        if (!seg_open_ && np) {                                                    /* the first pairs of a stretch: its source position and length are known now (after a seek, `base` is the target) */
            if (!cur_rate || ft.op.si.rate != cur_rate) { aout_set_rate(ft.op.si.rate, cymo_present_); cur_rate = ft.op.si.rate; mp.rate = ft.op.si.rate; mp.rate_changes++; }
            uint32_t b0 = (uint32_t)ft.base, rest = ft_total > b0 ? ft_total - b0 : 0u;
            tl_seg_t *g = tl_open(&mp_tl, mp.gen, b0, rest, 0u, 1u, 1u, 0u);
            if (!g) { aout_rollback(); ft.staged = 0; break; }
            seg_open_ = 1; seg_gain[g->gen & 3u].gen = g->gen; seg_gain[g->gen & 3u].g = dec_g; seg_gain[g->gen & 3u].id = lp.cur_id;
            if (fade_pending) { fade_pending = 0; heard_g = dec_g; heard_gain_gen = mp.gen; gain_apply_heard(); aout_fade_in(mp.gain_tgt); }
        }
        if (seg_open_ && mp_tl.n) {                                                 /* a seek whose first frame was damaged: the core recomputed the base against the next one; correct the still-empty segment */
            tl_seg_t *g0 = tl_at(&mp_tl, (uint8_t)(mp_tl.n - 1u)); uint32_t b1 = (uint32_t)ft.base;
            if (!g0->put && g0->src_start != b1) { g0->src_start = b1; g0->expect = ft_total > b1 ? ft_total - b1 : 0u; }
        }
        if (ev & F2_EV_FRAME_BAD) { aout_rollback(); mp.f2_rollbacks++; mp.errors++; continue; }
        if (np) {
            for (uint32_t i = 0; i < 2u * np; i++) {                              /* int32 -> the 16-bit ring (the rounding of the 24-bit path; a valid 16-bit stream needs none) */
                int32_t v = fl_out[i];
                if (bps > 16u) { const int sh = (int)(bps - 16u); v = (v + (1 << (sh - 1))) >> sh; } else if (bps < 16u) v <<= (int)(16u - bps);
                if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
                fl_pcm[i] = (int16_t)v;
            }
            uint32_t acc = (ev & F2_EV_SILENCE) ? aout_put(fl_pcm, np, 1) : aout_put_staged(fl_pcm, np, 1);
            if (acc < np) { mp.stalls++; mp.acct_bad++; }
            if (ev & F2_EV_SILENCE) { tl_put(&mp_tl, acc); mp.seg_put += acc; }
            done += np;
        }
        if (ev & F2_EV_FRAME_OK) {
            uint32_t n2 = aout_staged(); if (n2 > mp.f2_staged_max) mp.f2_staged_max = n2;
            aout_commit(); tl_put(&mp_tl, n2); mp.seg_put += n2;
            bad_run = 0; mp.frames++; mp.dec_us += ft_cyc / (CLK_HZ / 1000000u); mp.dec_blocks++; mp.dec_avg_us = mp.dec_us / mp.dec_blocks; ft_cyc = 0; mp.pairs_out = mp.seg_put;
        }
    }
    return 0;
}
#endif

static void dip_poll(void);
void mp3play_pump(void)
{
    stream_t *s = &lp_stream;
    dip_poll();
    io_service();
    lp.qn = q.n; lp.cur_pos = q.pos;
    if (mp.state != MPS_DEC) gov_publish(&mp_gov, 0, 0u, &mp.gov_mode, &mp.degrade);          /* idle, stopped, between tracks: the audio is not at risk, the meters and the UI get their full speed back at once */
    if (mp.state == MPS_WAITRATE || mp.state == MPS_ENDDRAIN) {           /* everything put must be heard before the rate may change or the list may end */
        aout_snap_t sn = aout_snap();
        if ((int32_t)(aout_heard(&sn) - mp_tl.put_total) >= 0) {
            if (mp.state == MPS_ENDDRAIN) { mp.state = MPS_IDLE; mp.playing = 0; lp.state = 0; }
            else { aout_flush(); aout_snap_t s2 = aout_snap(); tl_flush(&mp_tl, s2.cnt); start_from(queue_cur(&q)); }
        }
        return;
    }
    if (tempo_pend && mp.state == MPS_DEC && mp.cur_valid && mp.cur_gen == mp.gen) {
        const uint32_t s0 = mp.seeks; do_seek(0);
        if (mp.seeks != s0 || ++tempo_tries > 20u) tempo_pend = 0u;
    }
    if (mp.state != MPS_DEC) return;
    for (uint32_t k = 0; k < 2u; k++) {
        aout_snap_t sn = aout_snap(); uint32_t fill = aout_fill(&sn);
        gov_publish(&mp_gov, 1, fill, &mp.gov_mode, &mp.degrade);
        if (!gov_decode_wanted(&mp_gov, fill)) break;
#if P2_FLAC2
        if (is_flac) { if (flac_pass(s)) break; continue; }
#endif
        if (aout_room() < (tempo_q8[mp.tempo_idx] == 256u ? 1152u : TEMPO_OUT_CAP)) { mp.stalls++; break; }      /* the whole output of one decoded block must fit before it is decoded */
        pcm_blk_t b; uint32_t t0 = cycles();
        mp3d_step_t st = mp3d_step(&DEC, s, RING_PTR, &b);
        uint32_t dt = cycles() - t0; if (dt > mp.dec_max_cyc) mp.dec_max_cyc = dt; mp.dec_steps++;
        if (st == MS_BLOCK) { mp.dec_us += dt / (CLK_HZ / 1000000u); mp.dec_blocks++; mp.dec_avg_us = mp.dec_us / mp.dec_blocks; }
        mp.frames = DEC.frame_idx; mp.errors = DEC.errors; mp.resyncs = DEC.resyncs; mp.pairs_out = DEC.pairs_out; mp.pairs_total = DEC.pairs_total; mp.ch = DEC.ch;
        if (st == MS_BLOCK && skip_pending) {                    /* skip silence: the tail now (it needs the length), the lead as a seek, dropping this first block */
            skip_pending = 0; mp.skip_lead = skip_lead_p; mp.skip_tail = skip_tail_p;
            int ok = 0;
            if (skip_tail_p && mp3d_set_tail(&DEC, skip_tail_p)) ok = 1;
            if (skip_lead_p) { mp3_seek_t pl; if (mp3d_plan_seek(&DEC, skip_lead_p, &pl) && mp3d_seek(&DEC, s, &pl)) { ok = 1; mp.skip_applied++; continue; } else mp.skip_ign++; }
            if (ok) mp.skip_applied++;
        }
        if (st == MS_BLOCK) {
            const uint32_t q8 = tempo_q8[mp.tempo_idx], neutral = q8 == 256u;
            if (!neutral && !tempo_active) { tempo_start(&tempo_st, DEC.rate, (uint8_t)b.ch, q8); tempo_active = 1; }      /* (re)start the stretcher at the first block of a segment */
            if (!seg_open_) {
                if (!cur_rate || DEC.rate != cur_rate) {                 /* a rate we are not at: only legal on an empty ring (flushed or drained) */
                    aout_set_rate(DEC.rate, cymo_present_); cur_rate = DEC.rate; mp.rate = DEC.rate; mp.rate_changes++;
                }
                uint32_t rest = DEC.pairs_total > b.src_pos ? DEC.pairs_total - b.src_pos : 0u;
                tl_seg_t *g = tl_open(&mp_tl, mp.gen, b.src_pos, neutral ? rest : (uint32_t)(((uint64_t)rest * 256u) / q8), neutral ? 0u : 4096u, neutral ? 1u : 256u, neutral ? 1u : (uint16_t)q8, neutral ? 0u : TLF_TEMPO);
                if (!g) break;
                seg_open_ = 1; seg_gain[g->gen & 3u].gen = g->gen; seg_gain[g->gen & 3u].g = dec_g; seg_gain[g->gen & 3u].id = lp.cur_id;
            }
            if (fade_pending) { fade_pending = 0; heard_g = dec_g; heard_gain_gen = mp.gen; gain_apply_heard(); aout_fade_in(mp.gain_tgt); }
            uint32_t acc;
            if (neutral) acc = aout_put(b.pcm, b.pairs, b.ch == 2u);
            else {
                tempo_n = 0; tempo_total = 0; tempo_acc = 0;
                tempo_frame(&tempo_st, b.pcm, b.pairs * b.ch, b.ch == 2u); tempo_flush();
                acc = 0; mp.tempo_out = tempo_total;       /* (tl_put was done block by block) */
            }
            if (neutral) { tl_put(&mp_tl, acc); mp.seg_put += acc; if (acc < b.pairs) mp.stalls++; }
        } else if (st == MS_EOF) { track_ended(); break; }
        else if (st == MS_ERROR) { mp.state = MPS_IDLE; break; }
        else { mp.need_data++; break; }
    }
}

SMALL void mp3play_heard(void)
{
#if P2_FLAC2
    if (ft_active) { mp.f2_ferr = ft.rec.frame_errors; mp.f2_resync = ft.rec.resyncs; mp.f2_silent = (uint32_t)ft.rec.silent_pairs; mp.f2_wraps = ft.wraps; mp.f2_probes = ft.seek_probes; mp.f2_repos = ft.repositioned; }
#endif
#if P2_FLAC && TAU_LPC_FW
    mp.lpc_slots = tau_lpc_stat_samples; mp.lpc_timeout = tau_lpc_stat_timeout;
#endif
#if TAU_POLY_FW
    mp.poly_silent = tau_poly_stat_silent; mp.poly_fail = (tau_poly_stat_silent || !caps.poly) ? 1u : 0u;
    mp.poly_on = (uint32_t)tau_poly_hw_enable; mp.poly_slots = tau_poly_stat_slots; mp.poly_mismatch = tau_poly_stat_mismatch; mp.poly_timeout = tau_poly_stat_timeout;
#endif
    aout_snap_t s = aout_snap(); uint32_t h = aout_heard(&s);
    tl_advance(&mp_tl, h);
    const tl_seg_t *g = tl_current(&mp_tl, h);
    mp.cur_valid = g != 0;
    if (g) {
        mp.cur_gen = g->gen; mp.cur_pos = tl_pos(g, h);
        { uint32_t hid = seg_gain[g->gen & 3u].gen == g->gen ? seg_gain[g->gen & 3u].id : lp.cur_id;
          if (!np.valid || np.id != hid || np.gen == 0u) { np_refresh(g, hid); np.valid = 1; }
          np.pos = mp.cur_pos; np.rate = mp.rate;
          np.total = g->expect ? (g->num == g->den ? g->src_start + g->expect : g->src_start + (uint32_t)(((uint64_t)g->expect * g->den) / g->num)) : 0u; }
        if (g->gen != heard_gain_gen && seg_gain[g->gen & 3u].gen == g->gen) {      /* SEG_ENTER: the track now heard brings its own ReplayGain; the stage ramps over 5 ms */
            heard_gain_gen = g->gen; heard_g = seg_gain[g->gen & 3u].g; gain_apply_heard();
        }
    }
}

void mp3play_last_get(uint32_t *w) { w[0] = lastw[0]; w[1] = lastw[1]; w[2] = lastw[2]; }
void mp3play_last_restore(const uint32_t *w)
{
    lastp_t l; lastw[0] = lastw[1] = lastw[2] = 0u; last_boot[0] = w[0]; last_boot[1] = w[1]; last_boot[2] = w[2];
    if (lib_state != 1) { last_state = 5u; return; }
    if (!lastp_unpack(w, &l)) { last_state = (w[0] & 0x40000000u) ? 3u : 2u; return; }
    uint16_t *qb = library_queue_buf(); uint32_t st = 0;
    const uint32_t n = library_build_queue((uint8_t)l.kind, l.parent, 0, qb, 16384u, &st, 0);
    const int32_t pos = n ? lastp_find(qb, n, l.id) : -1;
    if (pos < 0) { last_state = 4u; return; }                            /* the library changed: the track is not in that queue any more */
    qsrc_kind = l.kind; qsrc_parent = l.parent;
    queue_set(&q, qb, n); q.pos = (uint32_t)pos; lp.qn = q.n; lp.cur_pos = q.pos; lp.shuffle = 0u;
    lastw[0] = w[0]; lastw[1] = w[1]; lastw[2] = w[2];
    np_refresh(0, l.id); np.valid = 1; last_state = 1u;                 /* Now Playing shows it, stopped */
}
void mp3play_settings_get(uint32_t *v) { v[0] = mp.volume; v[1] = q.repeat; v[2] = mp.rg_mode; v[3] = halcyon_saved; }
SMALL void mp3play_settings_set(const uint32_t *v)
{
    mp.volume = v[0]; q.repeat = (uint8_t)v[1]; lp.repeat = v[1];
    if (mp.rg_mode != v[2]) { mp.rg_mode = v[2]; rg_for_track(lp.cur_id); heard_g = dec_g; }
    gain_apply_heard();
    halcyon_restore(v[3]);                                       /* the saved preset (or one chosen in Core Settings) */
}

/* A gain dip around a change that would otherwise be heard as a step (a Halcyon recall): the stage ramps to zero in about 16 ms (a slower step), the caller changes, and the ramp back up follows at the same slow
 * step; the normal step is put back once the ramp up has finished (dip_poll, once per loop pass). */
#define GAIN_STEP_NORMAL 149u
#define GAIN_STEP_DIP 43u
static uint8_t dip_slow; static uint32_t dip_t0;
SMALL void mp3play_dip_begin(void)
{
    REG_WR(R_GAIN_STEP, GAIN_STEP_DIP); dip_slow = 1; dip_t0 = cycles();
    aout_gain_set(0u);
    const uint32_t t = cycles(); while ((uint32_t)(cycles() - t) < CLK_HZ / 1000u * 17u) { }
}
SMALL void mp3play_dip_end(void) { aout_gain_set(mp.gain_tgt); dip_t0 = cycles(); }
static void dip_poll(void) { if (dip_slow && (uint32_t)(cycles() - dip_t0) > CLK_HZ / 1000u * 40u) { REG_WR(R_GAIN_STEP, GAIN_STEP_NORMAL); dip_slow = 0; } }

/* ---- Now Playing data ---- */
static void np_copy(char *d, const char *s, uint32_t cap) { uint32_t i = 0; for (; s[i] && i + 1u < cap; i++) d[i] = s[i]; d[i] = 0; }
SMALL static void np_refresh(const tl_seg_t *g, uint32_t id)
{
    char t[48], a[40], b[40], q[32]; uint32_t cod = 0, ch = 0, kb = 0, hz = 0;
    library_meta(id, t, sizeof t, a, sizeof a, b, sizeof b);
    np_copy(np.title, t, sizeof np.title); np_copy(np.artist, a, sizeof np.artist); np_copy(np.album, b, sizeof np.album);
    uint32_t n = 0; q[0] = 0;
    if (library_facts(id, &cod, &ch, &kb, &hz)) {
        const char *cn = cod == 2u ? "FLAC " : "MP3 "; while (*cn) q[n++] = *cn++;
        if (kb && cod != 2u) { if (kb >= 100u) q[n++] = (char)('0' + kb / 100u); q[n++] = (char)('0' + (kb / 10u) % 10u); q[n++] = (char)('0' + kb % 10u); q[n++] = 'K'; q[n++] = ' '; }
        char k[8]; np_khz(k, hz); for (uint32_t i = 0; k[i]; i++) q[n++] = k[i]; q[n++] = ' ';
        const char *c = ch == 1u ? "MONO" : "STEREO"; while (*c) q[n++] = *c++;
        q[n] = 0;
    }
    np_copy(np.quality, q, sizeof np.quality);
    np.cover[0] = 0; if (!library_cover_path(id, np.cover, sizeof np.cover)) np.cover[0] = 0;
    np.id = id; np.gen++; (void)g;
}
uint32_t mp3play_degrade_now(void) { return mp.degrade; }
uint32_t mp3play_paused_now(void) { return (!mp.playing || mp.held) ? 1u : 0u; }
SMALL uint32_t mp3play_library(uint32_t *err, uint32_t *n_tracks) { *err = lib_err; *n_tracks = lib_n_tracks; return lib_state; }
SMALL const np_info_t *mp3play_np(void)
{
    np.fault = (uint32_t)(mp.poly_fail ? 1u : 0u);
    np.playing = mp.playing; np.held = mp.held; np.volume = mp.volume; np.rg_mode = mp.rg_mode; np.repeat = q.repeat; np.shuffle = lp.shuffle; np.varispeed = mp.speed_idx != 2u; np.tempo_q8 = np.varispeed ? (uint32_t)speed_num[mp.speed_idx] * 256u / speed_den[mp.speed_idx] : tempo_q8[mp.tempo_idx]; np.gov = mp.gov_mode; np.degrade = mp.degrade;
    return &np;
}

SMALL void mp3play_play_queue(uint32_t n, uint32_t start, uint32_t src_kind, uint32_t src_parent, int shuffled, uint32_t seed)
{
    if (!n) return;
    qsrc_kind = src_kind == LQ_ALL_SHUFFLED ? LQ_ALL_NATURAL : src_kind; qsrc_parent = src_parent;
    queue_set(&q, library_queue_buf(), n); q.pos = start < n ? start : 0u; q.seed = seed; q.shuffled = shuffled ? 1u : 0u; lp.shuffle = shuffled ? 1u : 0u;
    flush_and_start(queue_cur(&q));
}

SMALL void mp3play_io_quiesce(void)
{
    uint32_t t0 = cycles();
    while (lp_stream.fl.active) {
        if (stream_wait_idle(&lp_stream, cycles(), slot_read_poll())) break;
        if ((uint32_t)(cycles() - t0) > 4u * CLK_HZ) break;
    }
}

/* Diagnostics: the 16-bit I2S slot adds 6 dB in one sample, so the gain dips (muted, switched, faded back) around the change. */
SMALL void mp3play_a16_set(uint32_t on)
{
    if (!(REG_RD(R_AUDIO_CFG) & 2u)) return;                              /* a bitstream without the switch */
    aout_mute_wait(); REG_WR(R_AUDIO_CFG, on ? 1u : 0u); gain_apply_heard();
}
SMALL uint32_t mp3play_a16_get(void) { return REG_RD(R_AUDIO_CFG) & 1u; }
SMALL uint32_t mp3play_is_flac(void)
{
#if P2_FLAC
    return is_flac;
#else
    return 0;
#endif
}

SMALL uint32_t mp3play_pref_get(uint32_t id)
{
    switch (id) { case PREF_VOLUME: return mp.volume; case PREF_RG: return mp.rg_mode; case PREF_STRETCH: return mp.rate_mode; case PREF_SPEED: return mp.rate_mode == 1u ? mp.sel_pitch : mp.rate_mode == 2u ? mp.sel_stretch : 0u; case PREF_REPEAT: return q.repeat;
                  case PREF_SHUFFLE: return lp.shuffle ? 1u : 0u; case PREF_SEEKSTEP: return mp.seek_idx; case PREF_SKIPSIL: return mp.skip_on; }
    return 0;
}
SMALL void mp3play_pref_set(uint32_t id, uint32_t v)
{
    switch (id) {
    case PREF_VOLUME: mp.volume = v > 100u ? 100u : v; gain_apply_heard(); break;
    case PREF_RG: if (v < 3u && v != mp.rg_mode) { mp.rg_mode = v; rg_for_track(lp.cur_id); heard_g = dec_g; gain_apply_heard(); } break;
    case PREF_STRETCH: if (v < 3u && v != mp.rate_mode) { mp.rate_mode = v; rate_apply_mode(); } break;
    case PREF_SPEED: if (mp.rate_mode == 1u && v < 8u) { mp.sel_pitch = v; rate_apply_mode(); } else if (mp.rate_mode == 2u && v < 5u) { mp.sel_stretch = v; rate_apply_mode(); } break;
    case PREF_REPEAT: if (v < 3u) { q.repeat = (uint8_t)v; lp.repeat = v; } break;
    case PREF_SHUFFLE: shuffle_set(v); break;
    case PREF_SEEKSTEP: if (v < 4u) mp.seek_idx = v; break;
    case PREF_SKIPSIL: mp.skip_on = v ? 1u : 0u; break;                 /* takes effect at the next track opened */
    }
}
