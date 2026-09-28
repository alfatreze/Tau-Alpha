/* ==================================================================== MASTER VU meter (VIZ_VU_MASTER)
 * Mastering-style segmented dB peak ladder: docs/METER_VU_MASTERING_SPEC.md. Two independent horizontal
 * ladders (L above R), 24 segments each (-48..0 dBFS, 2 dB/segment, spec section 2.3), zone-coloured
 * green/yellow/red (section 2.4), a held-peak marker per channel, and an optional technical info overlay
 * below (section 5). Everything draws with fb_rect() (plain OP_RECT) -- OP_BAR is the wrong shape here
 * (horizontal, three colours, section 4.1) and no RTL exists or is needed for this meter.
 *
 * The dB-to-segment table and the "at most 3 coalesced rects" zone rule live in fw/vu_master_core.h
 * (portable, no MMIO/engine calls, host-tested by sim/test_vu_master.py). This file is the engine glue:
 * theme colours (fw/theme.h's OK/WARN/DANGER roles, the exact ones docs/THEME_SPEC.md already named for
 * this meter), peak-hold (mtr_peak_step, reused UNCHANGED from fw/meter_core.h per the resolved open
 * question in spec section 8 item 4 -- no new ballistics design), the overlay text, and the face/redraw
 * caching VIZ_VU itself established (section 4.3: static geometry drawn once, only lit segments and the
 * peak marker redraw).
 *
 * Box-rect contract: every coordinate is derived from the passed-in (x0, y0, w, h), never from
 * UI_WAVE_* literals, so this is fullscreen-ready by construction (spec section 3.2) -- wiring it into
 * fw/fullscreen.inc's capable list is a later pass (V5), not done here. */
#include "vu_master_core.h"
#include "vu_segment_table.h"

#define VUM_LABEL_W    18u    /* "L"/"R" label gutter, px */
#define VUM_ROW_H      12u    /* one ladder's height, px */
#define VUM_SCALE_Y    26u    /* dB scale line, offset from box top (spec section 3.1 layout table) */
#define VUM_ROW2_Y     28u    /* second (R) ladder's top, offset from box top */
#define VUM_OVERLAY_Y  42u    /* technical info overlay start, offset from box top */
#define VUM_SEG_GAP    1u     /* px between segments -- the discreteness IS the readout (section 2.1) */
#define VUM_PEAK_MARK_H 4u    /* held-peak marker height, px -- thinner than VUM_ROW_H so it reads as a
                                 * floating tick line, not a same-height block indistinguishable from a
                                 * rendering glitch (owner feedback item 1b) */

/* Fast display-level smoothing (owner feedback item 1a): NOT a reversal of section 2.2's "PPM-like fast
 * attack, not VU ballistics" decision -- VU ballistics means ~300ms integration; this is a much faster
 * digital smoothing purely to stop single-frame jitter ("blipping") on the raw instantaneous peak, the
 * same minimal integration real true-peak meters still apply. Reuses mtr_ease() (fw/meter_core.h)
 * UNCHANGED, the same primitive wviz_bars_tick() already uses for its own attack/release -- no new
 * ballistics curve invented. Mode 2 (exponential): rate is 1..100, k = 8 + rate*248/100.
 *   Attack  85 -> k=218 -> ~85% of the gap closed per 26ms tick (~2 ticks/52ms to converge): reads as
 *            near-instant like a real peak meter, but damps a single one-off sample outlier that would
 *            otherwise flash a segment on then straight back off.
 *   Release 40 -> k=107 -> ~42% of the gap closed per tick (~4 ticks/~100ms to settle): long enough that
 *            successive analysis windows don't make the ladder visibly flicker down-and-up, short enough
 *            it still feels responsive, not sluggish.
 * Smoothing runs on the SAME 0..255 index domain vum_peak_to_segments() shifts peak into (peak >> 7) --
 * not a second, independently-rounded scale -- so easing then shifting back up (<<7) before the existing
 * table lookup recovers the identical index with no precision loss. */
#define VUM_EASE_MODE    2u
#define VUM_EASE_ATTACK  85u
#define VUM_EASE_RELEASE 40u

static mtr_peak_t vum_peak[2];                          /* per channel peak-hold state (fw/meter_core.h) */
static uint8_t  vum_disp_idx[2];                        /* eased 0..255 segment-table index per channel */
static int16_t  vum_disp_vel[2];                        /* mtr_ease() spring-mode slot (unused by mode 2, required by the signature) */
static uint8_t  vum_lit_drawn[2]  = { 0xFFu, 0xFFu };    /* last drawn lit-segment count per channel */
static uint8_t  vum_peak_drawn[2] = { 0xFFu, 0xFFu };    /* last drawn held-peak segment per channel */
static uint32_t vum_last_sec = 0xFFFFFFFFu;              /* overlay's 1 Hz refresh latch (ui_sec) */

/* Fixed, non-configurable peak-hold shape (spec section 8 item 4: this meter deliberately does not
 * expose peak_hold_ms/peak_fall/peak_gravity as settings -- mastering meters conventionally have simple,
 * opinionated peak-hold). Values are winamp_bars' own GRAVITY/200ms/35 defaults (meters/winamp_bars/
 * meter.json's FLUID preset), reused verbatim rather than re-tuned. */
static const mtr_peak_cfg_t vum_peak_cfg = { 1u, 1u, 200u, 35u };

/* x position of segment `seg` (0-based) within a ladder of width `lw` starting at `lx`. Also returns the
 * per-segment width through *seg_w_out when non-null, since every caller needs it right after. */
COLD_FN3 static uint32_t vum_seg_x(uint32_t lx, uint32_t lw, uint32_t seg, uint32_t *seg_w_out)
{
    uint32_t span = VUM_SEG_GAP * (VUM_SEGMENTS - 1u);
    uint32_t seg_w = (lw > span) ? (lw - span) / VUM_SEGMENTS : 1u;
    if (seg_w_out) *seg_w_out = seg_w;
    return lx + seg * (seg_w + VUM_SEG_GAP);
}

/* Zone colour: THEME mode reads the role table directly (LED_LO/MIDC/HI are already th_role[TR_OK/WARN/
 * DANGER], fw/theme.h) -- the exact roles docs/THEME_SPEC.md names for "the VU ladder's green/amber/red".
 * CUSTOM mode reads the three raw RGB565 parameters. */
COLD_FN3 static uint16_t vum_zone_color(uint32_t zone)
{
    if (MV_VU_MASTER(COLOR_MODE) == 0u)
        return (zone == 0u) ? LED_LO : (zone == 1u) ? LED_MIDC : LED_HI;
    return (zone == 0u) ? (uint16_t)MV_VU_MASTER(COLOR_GREEN)
         : (zone == 1u) ? (uint16_t)MV_VU_MASTER(COLOR_YELLOW)
                         : (uint16_t)MV_VU_MASTER(COLOR_RED);
}

/* One channel's ladder + held-peak marker at (lx, ly), width lw, against flat background bg. peak is the
 * MTR_HEADROOM-scaled 16-bit accumulator (peak_l or peak_r). Per spec section 4.2: one background rect
 * for the whole ladder, then at most 3 coalesced lit-zone rects (vum_coalesce), skipping unlit segments
 * entirely -- cost scales with how loud the material is, not with the segment count. The held-peak
 * marker (section 2.4/6) is drawn only when it sits ABOVE the currently lit run (the same "float above
 * the bar" convention wviz_bars_tick()'s own peak cap already uses), in the colour of the zone it is
 * currently in -- no separate marker colour setting. */
COLD_FN3 static void vum_draw_ladder(uint32_t ch, uint32_t lx, uint32_t ly, uint32_t lw, uint16_t bg, uint32_t peak, uint32_t force)
{
    uint32_t idx = peak >> 7;
    if (idx > 255u) idx = 255u;
    if (force) {
        /* Snap on open/force (spec section 4.3's own rule: assume nothing about the pixels) rather than
         * ease in from a stale/zeroed index -- avoids a visible ramp-in the instant this meter is shown. */
        vum_disp_idx[ch] = (uint8_t)idx;
        vum_disp_vel[ch] = 0;
    } else {
        uint32_t rate = (idx >= vum_disp_idx[ch]) ? VUM_EASE_ATTACK : VUM_EASE_RELEASE;
        vum_disp_idx[ch] = mtr_ease(vum_disp_idx[ch], (uint8_t)idx, VUM_EASE_MODE, rate, &vum_disp_vel[ch]);
    }
    uint32_t lit = vum_peak_to_segments((uint32_t)vum_disp_idx[ch] << 7, vu_db_to_segment, VU_SEGMENT_TABLE_N);

    mtr_peak_step(&vum_peak[ch], (uint8_t)((lit * 255u) / VUM_SEGMENTS), &vum_peak_cfg, 26u);
    uint32_t peak_seg = ((uint32_t)vum_peak[ch].peak * VUM_SEGMENTS) / 255u;
    if (peak_seg > VUM_SEGMENTS) peak_seg = VUM_SEGMENTS;

    if (!force && lit == vum_lit_drawn[ch] && peak_seg == vum_peak_drawn[ch]) return;
    vum_lit_drawn[ch] = (uint8_t)lit;
    vum_peak_drawn[ch] = (uint8_t)peak_seg;

    fb_rect(lx, ly, lw, VUM_ROW_H, bg);

    uint32_t zone[3], start[3], count[3];
    uint32_t n = vum_coalesce(lit, zone, start, count);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t seg_w, x0 = vum_seg_x(lx, lw, start[i], &seg_w);
        uint32_t x1 = vum_seg_x(lx, lw, start[i] + count[i] - 1u, 0);
        fb_rect(x0, ly, (x1 + seg_w) - x0, VUM_ROW_H, vum_zone_color(zone[i]));
    }

    if (peak_seg > lit && peak_seg > 0u) {
        uint32_t seg_w, px = vum_seg_x(lx, lw, peak_seg - 1u, &seg_w);
        uint32_t mh = (VUM_ROW_H > VUM_PEAK_MARK_H) ? VUM_PEAK_MARK_H : VUM_ROW_H;
        uint32_t my = ly + (VUM_ROW_H - mh) / 2u;
        fb_rect(px, my, seg_w, mh, vum_zone_color(vum_zone_of(peak_seg)));
    }
}

/* Static geometry: "L"/"R" labels and the dB scale line with ticks at -40/-30/-20/-12/-6/-3/0 dBFS
 * (spec section 3.1). Drawn once per force -- VIZ_VU's own proven face-cache pattern (section 4.3),
 * learned the hard way there that redrawing static geometry every frame flickers during scanout. */
COLD_FN3 static void vum_draw_face(uint32_t x0, uint32_t y0, uint32_t w, uint16_t bg)
{
    uint32_t lx = x0 + VUM_LABEL_W, lw = (w > VUM_LABEL_W) ? w - VUM_LABEL_W : 1u;

    fb_rect(x0, y0, w, VUM_ROW_H, bg);
    fb_rect(x0, y0 + VUM_ROW2_Y, w, VUM_ROW_H, bg);
    fb_set_color(UI_DIM, bg);
    fb_char(x0, y0, 'L', TS_1X, TS_1X);
    fb_char(x0, y0 + VUM_ROW2_Y, 'R', TS_1X, TS_1X);

    uint32_t sy = y0 + VUM_SCALE_Y;
    fb_rect(lx, sy, lw, 1u, UI_FAINT);
    static const int32_t ticks_db[7] = { -40, -30, -20, -12, -6, -3, 0 };
    for (uint32_t t = 0; t < 7u; t++) {
        uint32_t seg = (uint32_t)((ticks_db[t] + 48) / 2);   /* dB to 0-based segment, section 2.3's formula */
        if (seg >= VUM_SEGMENTS) seg = VUM_SEGMENTS - 1u;
        uint32_t seg_w, tx = vum_seg_x(lx, lw, seg, &seg_w);
        fb_rect(tx, sy - 2u, (seg_w > 1u) ? 1u : seg_w, 5u, UI_FAINT);
    }
}

/* Technical info overlay (spec section 5): format/rate/bitrate/encoder/CPU always; DEC (decoder-only
 * CPU%) when this build has the decode-stage profiler (MP3_PROFILE/FLAC_PROFILE, diagnostic-profile
 * builds only); SDRAM-busy only when this bitstream has the counter (TAU_SDRAM_BUSY, a build-time
 * macro, matching CT_BLT's own honesty rule -- R_SDR_BUSY reads a hardwired 0 without it, so the row
 * is simply omitted rather than show a false "0%"). Refreshed once a second (ui_sec), same cadence as
 * the format-line row above.
 *
 * B-347: DEC sits ALONGSIDE the CPU row above, not in place of it -- they measure different things.
 * ui_cpu_pct() is whole-system non-idle time (decode + UI/meter drawing + everything else); DEC is
 * JUST the audio-decoder stages, so DEC <= CPU always, and (CPU - DEC) is everything else the firmware
 * spends time on. Both numbers are useful and neither substitutes for the other, so both are shown;
 * this overlay has no row-space pressure that would justify collapsing them into one.
 *
 * This is a THIRD, independent accumulator set (mp3_*_vum_cyc / flac_*_vum_cyc, third_party/libhelix-
 * mp3/pub/mp3_profile.h and fw/flac.h), not a second reader of the screen row's own set -- exactly the
 * B-089 precedent this comment used to cite as the reason this row was deferred. It advances from the
 * same tick()-around-the-call instrumentation, at zero extra runtime cost when this row isn't drawn
 * (MPROF_ADD/PROF_ADD are already no-ops when MP3_PROFILE/FLAC_PROFILE are 0). Read-and-reset happens
 * right here, once per this function's own once-a-second gate above (`ui_sec == vum_last_sec` already
 * skips the whole body on any other frame) -- no separate latch needed, same as the CPU/DEC-adjacent
 * rows already do it.
 *
 * MP3: sums all five stage buckets (Huffman/bit-read, Dequantize, AntiAlias, HybridTransform, Subband)
 * as a percent of realtime -- the same conversion (cycles / (CLK_HZ/100)) the screen row's H/I/S/D/A/X
 * already use, and these five cover the entire per-granule/per-channel decode path mp3dec.c times.
 *
 * FLAC: sums flac_res_vum_cyc + flac_lpc_vum_cyc (channel 0's residual/Rice-decode + reconstruction
 * passes) as a percent of realtime, the SAME conversion as MP3's -- deliberately NOT r_pct's own
 * formula (flac_res_cyc*100/(res+lpc)), which is an internal ratio between FLAC's own two passes, not
 * a share of realtime, and would read as a wildly wrong percentage here. unary()'s cost (u_pct's
 * basis) is NOT added on top: reading flac.c, PROF_T0()/PROF_ADD() around the residual() call already
 * wrap unary()'s own execution (residual() is what calls unary()), so its cost is already counted
 * inside flac_res_cyc/flac_res_vum_cyc -- adding u_est again would double-count it. This is confirmed
 * by player.c's own comment on u_pct: it is described as "a share of the same flac_res_cyc+flac_lpc_cyc
 * denominator," i.e. a subset already inside the total, not an additional cost sitting outside it.
 * Honest limitation, not fixed here: flac.c only times channel 0 (the file's own comment on subframe()
 * says channel 1 "does the same work, so the ratio carries" for r_pct's internal ratio, but this
 * doesn't hold for an absolute percent-of-realtime figure) and only the residual/reconstruction passes,
 * not frame-header parsing or output scaling -- so FLAC's DEC% is a real but likely-understated lower
 * bound on true decoder cost, unlike MP3's DEC%, which covers the whole decode path. */
COLD_FN3 static void vum_draw_overlay(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint16_t bg, uint32_t force)
{
    if (!MV_VU_MASTER(INFO)) {
        if (force) fb_rect(x0, y0, w, h, bg);
        return;
    }
    if (!force && ui_sec == vum_last_sec) return;
    vum_last_sec = ui_sec;

    fb_rect(x0, y0, w, h, bg);
    fb_set_color(UI_DIM, bg);
    uint32_t row_h = FB_CELL(TS_1X), ry = y0;

    char b[48], *q;

    /* Format / bitrate / rate, FLAC also carries bit-depth (section 5.2's field table; MP3 has no
     * bit-depth concept and the row is simply not appended, per the resolved open question item 5). */
    q = b;
    *q++ = (track_fmt == FMT_FLAC) ? 'F' : 'M';
    *q++ = (track_fmt == FMT_FLAC) ? 'L' : 'P';
    *q++ = (track_fmt == FMT_FLAC) ? 'A' : '3';
    if (track_fmt == FMT_FLAC) *q++ = 'C';
    *q++ = ' ';
    if (track_kbps) { q = ui_dec(q, track_kbps); *q++ = 'K'; *q++ = ' '; }
    q = ui_dec(q, track_hz / 1000u);
    *q++ = '.'; q = ui_dec(q, (track_hz % 1000u) / 100u);
    *q++ = 'K'; *q++ = 'H'; *q++ = 'Z';
    if (track_fmt == FMT_FLAC) { *q++ = ' '; q = ui_dec(q, fl_bps_mirror); *q++ = 'B'; }
    *q = 0;
    fb_text_clipped(x0, ry, b, TS_1X, TS_1X, w);
    ry += row_h;

    if (track_encoder[0]) {
        fb_text_clipped(x0, ry, track_encoder, TS_1X, TS_1X, w);
        ry += row_h;
    }

    q = b; *q++ = 'C'; *q++ = 'P'; *q++ = 'U'; *q++ = ' '; q = ui_dec(q, ui_cpu_pct()); *q++ = '%'; *q = 0;
    fb_text_clipped(x0, ry, b, TS_1X, TS_1X, w);
    ry += row_h;

#if MP3_PROFILE || FLAC_PROFILE
    {
        uint32_t dec_pct = 0u;
#if MP3_PROFILE
        if (track_fmt == FMT_MP3) {
            uint32_t sum = mp3_huff_vum_cyc + mp3_dequant_vum_cyc + mp3_alias_vum_cyc
                         + mp3_xform_vum_cyc + mp3_sub_vum_cyc;
            dec_pct = sum / (CLK_HZ / 100u);
            mp3_huff_vum_cyc = mp3_dequant_vum_cyc = mp3_alias_vum_cyc = 0u;
            mp3_xform_vum_cyc = mp3_sub_vum_cyc = 0u;
        }
#endif
#if FLAC_PROFILE
        if (track_fmt == FMT_FLAC) {
            uint32_t sum = flac_res_vum_cyc + flac_lpc_vum_cyc;
            dec_pct = sum / (CLK_HZ / 100u);
            flac_res_vum_cyc = flac_lpc_vum_cyc = 0u;
        }
#endif
        q = b; *q++ = 'D'; *q++ = 'E'; *q++ = 'C'; *q++ = ' ';
        q = ui_dec(q, dec_pct); *q++ = '%'; *q = 0;
        fb_text_clipped(x0, ry, b, TS_1X, TS_1X, w);
        ry += row_h;
    }
#endif

#if TAU_SDRAM_BUSY
    {
        static uint32_t vum_busy0, vum_sec0 = 0xFFFFFFFFu;
        if (vum_sec0 == 0xFFFFFFFFu) { vum_sec0 = ui_sec; vum_busy0 = REG(R_SDR_BUSY); }
        uint32_t dt = ui_sec - vum_sec0;
        if (dt) {
            uint32_t delta = REG(R_SDR_BUSY) - vum_busy0;
            uint32_t total = dt * (SDR_CLK_HZ / 100u);   /* window length in clk_sdram cycles / 100, for a direct percent */
            uint32_t pct = total ? delta / total : 0u;
            if (pct > 100u) pct = 100u;
            q = b; *q++ = 'S'; *q++ = 'D'; *q++ = 'R'; *q++ = 'A'; *q++ = 'M'; *q++ = ' ';
            q = ui_dec(q, pct); *q++ = '%'; *q = 0;
            fb_text_clipped(x0, ry, b, TS_1X, TS_1X, w);
            vum_sec0 = ui_sec; vum_busy0 = REG(R_SDR_BUSY);
        }
    }
#endif
}

/* Entry point, matching the draw contract (fw/meter.h) every fullscreen-ready meter uses (wviz_bars_tick()/
 * wviz_scope_tick()/chladni_tick_box()). in->force mirrors wviz_force (B-234's shared "context just changed"
 * flag, documented as consumed by whichever tick runs next -- only one viz_mode is ever active per frame);
 * clearing the real global stays this function's job, same as before. */
COLD_FN3 static void vum_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y0 = in->y, w = in->w, h = in->h;
    const uint16_t bg = in->bg;
    uint32_t force = in->force;
    if (force) {
        vum_lit_drawn[0] = vum_lit_drawn[1] = 0xFFu;
        vum_peak_drawn[0] = vum_peak_drawn[1] = 0xFFu;
        vum_last_sec = 0xFFFFFFFFu;
        vum_draw_face(x0, y0, w, bg);
        wviz_force = 0u;
    }

    uint32_t lx = x0 + VUM_LABEL_W, lw = (w > VUM_LABEL_W) ? w - VUM_LABEL_W : 1u;
    vum_draw_ladder(0u, lx, y0, lw, bg, in->peak_l, force);
    vum_draw_ladder(1u, lx, y0 + VUM_ROW2_Y, lw, bg, in->peak_r, force);

    if (h > VUM_OVERLAY_Y)
        vum_draw_overlay(x0, y0 + VUM_OVERLAY_Y, w, h - VUM_OVERLAY_Y, bg, force);
}
