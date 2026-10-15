/* fw/pcm_ring_fw.inc -- gapless step G1: the decoders write into the hardware PCM ring instead of the audio FIFO (B-746; hardware src/fpga/core/tau_pcm_ring.sv, MMIO 0x184-0x198;
 * design docs/features/PCM_RING_HW_DESIGN.md; the pure put side is fw/pcm_ring_hw.h, host-tested). Everything here is behind TAU_PCM_RING_FW (default 0) and inert unless the boot
 * probe finds the unit AND the hardware gain stage owns the gain (the ring carries RAW samples: the gain is applied at the FIFO output, so a software gain would be skipped).
 * With the ring on: the CPU stores pairs into the PSRAM window and publishes an index; the hardware keeps the FIFO topped up by itself, so no firmware stall shorter than the ring's
 * fill underruns. The clock and the software meters follow the HEARD position (what the FIFO has released), not the decoded one. Pause HOLDS the reader; a natural end plays the ring out
 * before the next track loads (G2 will replace that wait with a boundary marker). Anything that flushes (track change, seek) drops the unread pairs in hardware. */
#if TAU_PCM_RING_FW
/* RG_SIZE, RG_MARGIN, RG_BASE_W and the R_RING_* registers are defined at the top of player.c (pcm_flush, the clock and Info > RING need them before this file). */
#ifndef RG_WIN
#define RG_WIN ((volatile uint32_t *)(uintptr_t)(0xA4000000u + 4u * RG_BASE_W))
#endif
COLD_FN2 static uint32_t rg_rd_ridx(void) { return REG(R_RING_RIDX); }
COLD_FN2 static void     rg_wr_word(uint32_t i, uint32_t w) { RG_WIN[i] = w; }
COLD_FN2 static void     rg_wr_wptr(uint32_t i) { REG(R_RING_WPTR) = i; }
COLD_FN2 static int rg_spin_common(int abortable)
{
    const uint32_t t0 = cycles();
    poll_input();
    refill_pump();
    { const uint32_t d = cycles() - t0; fl_idle_cyc += d; LD_WAIT(d); }
    return (abortable && reload_pending) ? 1 : 0;
}
COLD_FN2 static int rg_spin_ab(void) { return rg_spin_common(1); }
COLD_FN2 static int rg_spin_na(void) { return rg_spin_common(0); }
static const prh_hooks_t rg_hk_ab = { rg_rd_ridx, rg_wr_word, rg_wr_wptr, rg_spin_ab };
static const prh_hooks_t rg_hk_na = { rg_rd_ridx, rg_wr_word, rg_wr_wptr, rg_spin_na };

COLD_FN2 static uint8_t rg_put_frame(const int16_t *pcm_, uint32_t pairs, uint32_t stereo) { rg_room1 = 0u; rg_put_total += pairs; return prh_put(&rg, &rg_hk_ab, pcm_, pairs, stereo); }
/* Slow path of the one-pair put (the cached room ran out): waits for room like prh_put1, then caches what is free so the next pairs take the inline fast path in cymo_push(). */
COLD_FN2 static uint8_t rg_put1(int32_t l, int32_t r, uint8_t abortable)
{
    const prh_hooks_t *h = abortable ? &rg_hk_ab : &rg_hk_na;
    uint32_t room;
    while ((room = prh_room(&rg, h->rd_ridx())) == 0u) { h->wr_wptr(rg.w); if (h->spin()) return 0u; }
    h->wr_word(rg.w, ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l);
    if (++rg.w == rg.size) rg.w = 0u;
    rg_n1++; rg_put_total++;
    rg_room1 = room - 1u;
    if ((rg_n1 & 63u) == 0u) h->wr_wptr(rg.w);
    return 1u;
}
COLD_FN2 static void rg_publish(void) { REG(R_RING_WPTR) = rg.w; }
COLD_FN2 static void rg_hold(uint8_t h)
{
    if (!rg_on || rg_held == h) return;
    rg_held = h;
    REG(R_RING_CTRL) = 1u | ((uint32_t)h << 1);
}
COLD_FN2 static void rg_arm(void)
{
    REG(R_RING_CTRL) = 0u;                        /* disabled: the reader follows the write index */
    REG(R_RING_BASE) = RG_BASE_W; REG(R_RING_SIZE) = RG_SIZE; REG(R_RING_WPTR) = 0u;
    for (uint32_t n = 0; n < 1000u && REG(R_RING_RIDX) != 0u; n++) { }
    prh_init(&rg, RG_SIZE, RG_MARGIN, 0u); rg_room1 = 0u;
    rg_held = 0u; rg_n1 = 0u; rg_min = RG_SIZE; rg_und = 0u;
    REG(R_RING_CTRL) = 1u;
    rg_fed = REG(R_RING_CNT); rg_put_total = rg_fed;
    gp_want = 0u; gp_pend = 0u; gp_mode = 0u;
    rg_on = 1u;
}
/* Back to the direct FIFO path (a hardware gain hand-over to software, the tempo stretcher): the unread ring is dropped and the FIFO flushed. */
COLD_FN2 static void rg_off(void)
{
    if (!rg_on) return;
    rg_on = 0u; gp_want = 0u; gp_pend = 0u; gp_mode = 0u;
    REG(R_RING_CTRL) = 0u;
    pcm_flush();
}
/* At a track load while the ring is off (the first one after the cold image is up, and again after a tempo or hardware-gain excursion). */
COLD_FN2 static void rg_probe(void)
{
    if (!(REG(R_RING_CTRL) >> 31) || !vol_st.hw) return;
    volatile uint32_t *a = RG_WIN, *b = RG_WIN + (RG_SIZE - 1u);          /* the window must answer before the hardware is trusted with it */
    a[0] = 0x5A5AC3C3u; b[0] = 0xA5A53C3Cu; a[1] = 0x12345678u;
    if (a[0] != 0x5A5AC3C3u || b[0] != 0xA5A53C3Cu || a[1] != 0x12345678u) return;
    rg_arm();
}
/* A natural end: let everything decoded play out (the ring and the FIFO) before the next track's load flushes. Pause holds the reader; input and a reload request leave the wait. */
COLD_FN2 static void rg_playout(void)
{
    if (!rg_on) return;
    rg_publish();
    const uint32_t t0 = cycles();
    for (;;) {
        poll_input();
        rg_hold(paused ? 1u : 0u);
        if (prh_playout_step(reload_pending, paused ? 1u : 0u, REG(R_RING_RIDX) == rg.w, PCM_EMPTY(REG(R_PCM_ST)), cycles() - t0, CLK_HZ * 5u)) break;
    }
    rg_hold(0u);
}
/* The software meters at the heard position: the pairs the FIFO has released since the last call, read back from the ring (they are within the margin, so never overwritten). */
COLD_FN2 static void rg_meter_feed(void)
{
    const uint32_t cnt = REG(R_RING_CNT), st = REG(R_PCM_ST);
    uint32_t pos, nf;
    const uint32_t n = prh_meter_span(cnt, PCM_LEVEL(st), rg_fed, REG(R_RING_RIDX), RG_SIZE, 1152u, &pos, &nf);
    {   /* Info > RING: the lowest the ring has been since it was armed, and the FIFO running dry while the ring still held pairs (the reader fell behind: it must stay 0) */
        const uint32_t fill = prh_fill(&rg, REG(R_RING_RIDX));
        if (fill < rg_min) rg_min = fill;
        if (PCM_EMPTY(st) && fill && rg_und < 65535u) rg_und++;
    }
    if (!n) return;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t w = RG_WIN[pos];
        pcm[2 * i] = (short)(w & 0xFFFFu); pcm[2 * i + 1] = (short)(w >> 16);
        if (++pos == RG_SIZE) pos = 0u;
    }
    rg_fed = nf;
    meters_feed(pcm, (int)(2u * n), 1);
}
/* Pairs decoded but not yet heard, for the clock. */
COLD_FN2 static uint32_t rg_lead(void) { return prh_lead(&rg, REG(R_RING_RIDX), PCM_LEVEL(REG(R_PCM_ST))); }

/* ============================ G2b: the gapless boundary (docs/features/GAPLESS_SPEC.md section 11) ============================
 * The next track is opened and decoded while the old one still plays out of the ring; the screen, the clock base and the ReplayGain follow when the old track has been HEARD to its end.
 * Same sample rate and same codec only (MP3 to MP3, FLAC to FLAC, via the library queue); everything else falls back to today's play-out and flush path (gp_abort). */
typedef struct {
    char title[TITLE_MAX], last_title[TITLE_MAX], artist[64], album[64], year[8], trk[8];
    int title_status;
    uint32_t secs, kbps, hz, sr, spf, bps, rgf, rgcap, art_ready, art_x, art_shown, art_file_id, art_sig, art_bad_sig;
    rg_t rg;
    uint8_t art_have, art_bad, art_bad_code;
} gp_disp_t;
#ifndef GP_SHADOW_ATTR
#define GP_SHADOW_ATTR __attribute__((section(".psram_state.gp")))
#endif
static gp_disp_t gp_old GP_SHADOW_ATTR;   /* (an attribute after a declarator list applies to the last one only: that put gp_old in hot RAM once) */
static gp_disp_t gp_new GP_SHADOW_ATTR;
COLD_FN2 static void gp_cp(void *d, const void *s_, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s_; while (n--) *a++ = *b++; }
COLD_FN2 static void gp_save(gp_disp_t *d)
{
    gp_cp(d->title, track_title, sizeof d->title); gp_cp(d->last_title, last_title, sizeof d->last_title);
    gp_cp(d->artist, track_artist, sizeof d->artist); gp_cp(d->album, track_album, sizeof d->album);
    gp_cp(d->year, track_year, sizeof d->year); gp_cp(d->trk, track_trk, sizeof d->trk);
    d->title_status = title_status;
    d->secs = track_secs; d->kbps = track_kbps; d->hz = track_hz; d->sr = samprate; d->spf = samp_per_frame; d->bps = bytes_per_sec;
    d->rg = rg_cur; d->rgf = rg_factor; d->rgcap = rg_cap_hw;
    d->art_ready = art_ready; d->art_x = art_x; d->art_shown = art_shown; d->art_file_id = art_file_id; d->art_sig = art_sig; d->art_bad_sig = art_bad_sig;
    d->art_have = art_have; d->art_bad = art_bad; d->art_bad_code = art_bad_code;
}
COLD_FN2 static void gp_load(const gp_disp_t *d)
{
    gp_cp(track_title, d->title, sizeof d->title); gp_cp(last_title, d->last_title, sizeof d->last_title);
    gp_cp(track_artist, d->artist, sizeof d->artist); gp_cp(track_album, d->album, sizeof d->album);
    gp_cp(track_year, d->year, sizeof d->year); gp_cp(track_trk, d->trk, sizeof d->trk);
    title_status = d->title_status;
    track_secs = d->secs; track_kbps = d->kbps; track_hz = d->hz; samprate = d->sr; samp_per_frame = d->spf; bytes_per_sec = d->bps;
    rg_cur = d->rg; rg_factor = d->rgf; rg_cap_hw = d->rgcap;
    art_ready = d->art_ready; art_x = d->art_x; art_shown = d->art_shown; art_file_id = d->art_file_id; art_sig = d->art_sig; art_bad_sig = d->art_bad_sig;
    art_have = d->art_have; art_bad = d->art_bad; art_bad_code = d->art_bad_code;
}
/* The end of the old track's file was reached (called just before lib_advance_auto()): everything it decoded is in the ring, none of the next. */
COLD_FN2 static void rg_und_note_cold(void) { rg_ux_n++; rg_ux_w = rg.w; rg_ux_r = REG(R_RING_RIDX); rg_ux_t = cycles(); rg_ux_ho = gp_applied; }
COLD_FN2 static void gp_eof_begin(void)
{
    gp_au_put = rg_put_total - gp_au_base; gp_au_n++;
    gp_sw_fl = 0u;
    gp_want = 0u;
    if (!rg_on || !lib_src) return;
    rg_publish();
    gp_marker = rg_put_total;
    gp_pre_pp = ui_sec * samprate + ui_sec_acc;
    gp_old_fmt = (uint8_t)track_fmt;
    gp_old_rate = (track_fmt == FMT_FLAC) ? fl.rate : track_hz;
    gp_want = 1u;
}
/* Just before the reload gate calls load_track(): enter gapless mode if the end-of-file branch asked for it. */
COLD_FN2 static void gp_begin_load(void)
{
    gp_mode = 0u;
    if (!gp_want) return;
    gp_want = 0u;
    if (!rg_on) return;
    gp_save(&gp_old);
    gp_mode = 1u;
}
/* Gapless is not possible after all (another codec or rate, or the file would not open): hand over to the ordinary path -- the old track plays out, the FIFO is flushed -- and carry on as a normal load. */
COLD_FN2 static void gp_abort(void)
{
    if (!gp_mode) return;
    gp_mode = 0u;
    if (gp_aborted < 65535u) gp_aborted++;
    rg_playout();
    pcm_flush();
    paused = 0;
    vol_apply();
}
/* After load_track() returned in gapless mode. Success: keep the old display on screen (the new one waits in gp_new), mark the boundary pending and tell the caller to take the gapless hand-off instead of
 * the flushing restart (return 1). Failure: put the old display back, the old tail keeps playing, the error screen follows (return 0). */
COLD_FN2 static int gp_end_load(int opened)
{
    if (!gp_mode) return 0;                         /* aborted inside load_track: it ran as an ordinary load */
    gp_mode = 0u;
    if (!opened) { gp_load(&gp_old); return 0; }
    gp_save(&gp_new);
    gp_load(&gp_old);
    gp_pend = 1u;
    return 1;
}
/* The restart body (stop_req) without its flush and without touching the screen: rewind to the start of the file, refill, decode from the first frame (the trim removes the priming). */
COLD_FN2 static void gp_handoff(void)
{
    gp_au_base = rg_put_total;   /* the new track's own pairs start here */
    rg_ho_t = cycles() | 1u;   /* Info > UND CTX (|1: zero means none yet) */
    refill_drain();
    if (track_fmt == FMT_FLAC) {
        if (!flac_restart()) { st0 |= (1u << 4); REG(R_STAT0) = st0; }
        frames = 0; min_level = 0xFFFFFFFFu;
        ui_sec = 0; ui_sec_acc = 0;
        return;
    }
    file_pos  = audio_start;
    ring_fill = 0; ring_rd = 0;
    frames = 0; min_level = 0xFFFFFFFFu;
    mt_wu = 0;
    ui_sec = 0; ui_sec_acc = 0;
    if (!prefill()) { st0 |= (1u << 4); REG(R_STAT0) = st0; }
}
/* Once per UI tick while a boundary is pending: when the old track has been heard to its end (or a flush brought the marker forward), the new track's display replaces it. Not while an overlay holds the draw engine. */
COLD_FN2 static void gp_tick(void)
{
    if (FB_HELD()) return;
    const uint32_t heard = REG(R_RING_CNT) - PCM_LEVEL(REG(R_PCM_ST));
    if (!prh_marker_reached(heard, gp_marker)) return;
    gp_sw_sec = ui_sec; gp_sw_len = track_secs; gp_sw_d = (int32_t)(heard - gp_marker); gp_sw_fill = prh_fill(&rg, REG(R_RING_RIDX)); gp_sw_n++;
    gp_pend = 0u;
    gp_load(&gp_new);
    vol_apply();                                     /* the new track's ReplayGain: the stage ramps over 5 ms, so two gains meet without a step */
    uint32_t tphase = cycles();
    load_ui_tail(&tphase);
    ui_boot_cancel();
    ui_last_sec = 0xFFFFFFFFu; ui_prog_sec = 0xFFFFFFFFu; ui_last_pause = 0xFFFFFFFFu; ui_mode_dirty = 1u;
    if (gp_applied < 65535u) gp_applied++;
}
#endif
