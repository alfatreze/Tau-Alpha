/* vu_master: a port of vum_tick() in fw/vu_master.inc (docs/METER_VU_MASTERING_SPEC.md) on the shared core twin. ctx = {fb, x, y, w, h,
 * bg, theme, p (params by key), st (module state), force}. Two channel ladders (L above R), 24 dB segments each, zone-coloured
 * green/yellow/red via ctx.theme.role.ok/warn/danger (fw/theme.h's TR_OK/WARN/DANGER, the roles docs/THEME_SPEC.md names for this
 * meter), a held-peak marker per channel reusing C.newPeak()/C.peakStep() -- the SAME ballistics fw/vu_master.inc reuses from
 * fw/meter_core.h, per the resolved open question in the spec (no new peak-hold design). The displayed level is also eased
 * (C.ease(), mode 2/exponential, attack 85/release 40 -- fw/vu_master.inc's VUM_EASE_* comment has the full reasoning) before
 * it is quantized to a segment count, to kill single-frame jitter without reversing section 2.2's fast-attack decision. The
 * 256-entry dB-to-segment lookup is
 * fw/vu_segment_table.h's checked-in vu_db_to_segment[], inlined verbatim below rather than re-derived from the log10 formula --
 * the generated table is the ground truth (tools/gen_vu_segment_table.py), so re-deriving it risks a second, independently-rounded
 * table that quietly disagrees with the firmware's.
 *
 * ctx.vu = { l, r } supplies the two peak magnitudes (0..32767ish, the MTR_HEADROOM-scaled 16-bit accumulator peak_l/peak_r read
 * fw/player.c:4500-4502) -- tau_run.js passes this through from the source frame the same way it already passes ctx.wave.
 *
 * The technical info overlay (fw/vu_master.inc's vum_draw_overlay, spec section 5) reads track_fmt/track_kbps/track_encoder/
 * ui_cpu_pct()/R_SDR_BUSY -- none of which exist in this browser lab (no decoded track, no MMIO), and tau_fb.js has no text-drawing
 * primitive any ported meter uses (winamp_bars/winamp_scope draw only rect/bar). Per the task's own guidance this is left as a
 * placeholder: when INFO is on, the overlay draws only the one background-clear rect vum_draw_overlay() issues before its (here
 * unported) text rows, plus a static "MP3 320K 44.1KHZ" demo caption for visual tuning -- the caption is drawn straight onto the
 * pixel buffer (fb._fill, no ctx.fb.rect/bar call) so it is NOT counted as an engine command and does not appear in the golden log,
 * matching the golden harness's own stub (fb_text_clipped/fb_char there are no-ops that emit nothing). A real text primitive and a
 * faithful overlay port are a later pass. */
(function (root) {
  const C = (typeof module !== 'undefined') ? require('../tau_core.js') : root.TauCore;
  const VUM_SEGMENTS = 24, VUM_ZONE_GREEN_MAX = 16, VUM_ZONE_YELLOW_MAX = 21;
  const VUM_LABEL_W = 18, VUM_ROW_H = 12, VUM_ROW2_Y = 28, VUM_OVERLAY_Y = 42, VUM_SEG_GAP = 1;
  const VUM_PEAK_MARK_H = 4;   // thinner held-peak marker (owner feedback item 1b), see fw/vu_master.inc's mirror comment
  const VUM_EASE_MODE = 2, VUM_EASE_ATTACK = 85, VUM_EASE_RELEASE = 40;   // owner feedback item 1a, mirrors fw/vu_master.inc exactly
  const VU_SEGMENT_TABLE = [
    0, 2, 4, 5, 6, 7, 8, 9, 9, 10, 10, 11, 11, 11, 12, 12,
    12, 12, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 15, 15, 15,
    15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19,
    19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
  ];
  function peakToSegments(peak) {
    let idx = peak >> 7; if (idx >= VU_SEGMENT_TABLE.length) idx = VU_SEGMENT_TABLE.length - 1;
    return VU_SEGMENT_TABLE[idx];
  }
  function zoneOf(seg) { return seg <= VUM_ZONE_GREEN_MAX ? 0 : seg <= VUM_ZONE_YELLOW_MAX ? 1 : 2; }
  function coalesce(lit) {
    if (lit > VUM_SEGMENTS) lit = VUM_SEGMENTS;
    const bounds = [0, VUM_ZONE_GREEN_MAX, VUM_ZONE_YELLOW_MAX, VUM_SEGMENTS], out = [];
    for (let z = 0; z < 3; z++) {
      const lo = bounds[z], hi = bounds[z + 1], segLo = lo, segHi = lit < hi ? lit : hi;
      if (segHi > segLo) out.push({ zone: z, start: segLo, count: segHi - segLo });
    }
    return out;
  }
  function segX(lx, lw, seg) {
    const span = VUM_SEG_GAP * (VUM_SEGMENTS - 1), segW = lw > span ? Math.trunc((lw - span) / VUM_SEGMENTS) : 1;
    return { x: lx + seg * (segW + VUM_SEG_GAP), w: segW };
  }
  const VUM_PEAK_CFG = { on: 1, gravity: 1, hold_ms: 200, fall: 35 };
  function state() {
    return {
      pk: [C.newPeak(), C.newPeak()],
      dispIdx: [0, 0], dispVel: [{ v: 0 }, { v: 0 }],
      litDrawn: [0xFF, 0xFF], peakDrawn: [0xFF, 0xFF],
    };
  }
  function zoneColor(theme, p, zone) {
    if (p.color_mode === 0) return zone === 0 ? theme.role.ok : zone === 1 ? theme.role.warn : theme.role.danger;
    return zone === 0 ? p.color_green : zone === 1 ? p.color_yellow : p.color_red;
  }
  function drawLadder(fb, ch, lx, ly, lw, bg, peak, theme, p, st, force) {
    let idx = peak >> 7; if (idx > 255) idx = 255;
    if (force) {
      st.dispIdx[ch] = idx; st.dispVel[ch].v = 0;
    } else {
      const rate = idx >= st.dispIdx[ch] ? VUM_EASE_ATTACK : VUM_EASE_RELEASE;
      st.dispIdx[ch] = C.ease(st.dispIdx[ch], idx, VUM_EASE_MODE, rate, st.dispVel[ch]);
    }
    const lit = peakToSegments(st.dispIdx[ch] << 7);
    C.peakStep(st.pk[ch], Math.trunc((lit * 255) / VUM_SEGMENTS), VUM_PEAK_CFG, 26);
    let peakSeg = Math.trunc((st.pk[ch].peak * VUM_SEGMENTS) / 255); if (peakSeg > VUM_SEGMENTS) peakSeg = VUM_SEGMENTS;
    if (!force && lit === st.litDrawn[ch] && peakSeg === st.peakDrawn[ch]) return;
    st.litDrawn[ch] = lit; st.peakDrawn[ch] = peakSeg;
    fb.rect(lx, ly, lw, VUM_ROW_H, bg);
    for (const seg of coalesce(lit)) {
      const a = segX(lx, lw, seg.start), b = segX(lx, lw, seg.start + seg.count - 1);
      fb.rect(a.x, ly, (b.x + b.w) - a.x, VUM_ROW_H, zoneColor(theme, p, seg.zone));
    }
    if (peakSeg > lit && peakSeg > 0) {
      const px = segX(lx, lw, peakSeg - 1);
      const mh = VUM_ROW_H > VUM_PEAK_MARK_H ? VUM_PEAK_MARK_H : VUM_ROW_H;
      const my = ly + Math.trunc((VUM_ROW_H - mh) / 2);
      fb.rect(px.x, my, px.w, mh, zoneColor(theme, p, zoneOf(peakSeg)));
    }
  }
  /* Static geometry (L/R labels, dB scale ticks): the label/tick glyphs themselves have no JS text primitive (see header note), so
   * only the scale line + tick marks (real fb.rect calls, counted) are ported; the "L"/"R" glyphs are a visual-only placeholder
   * painted straight into the pixel buffer, uncounted -- matching the golden harness's fb_char() stub emitting nothing. */
  function drawFace(fb, x0, y0, w, bg, theme) {
    const lx = x0 + VUM_LABEL_W, lw = w > VUM_LABEL_W ? w - VUM_LABEL_W : 1;
    fb.rect(x0, y0, w, VUM_ROW_H, bg);
    fb.rect(x0, y0 + VUM_ROW2_Y, w, VUM_ROW_H, bg);
    const sy = y0 + 26;
    fb.rect(lx, sy, lw, 1, theme.role.faint);
    const ticksDb = [-40, -30, -20, -12, -6, -3, 0];
    for (const db of ticksDb) {
      let seg = Math.trunc((db + 48) / 2); if (seg >= VUM_SEGMENTS) seg = VUM_SEGMENTS - 1;
      const t = segX(lx, lw, seg);
      fb.rect(t.x, sy - 2, t.w > 1 ? 1 : t.w, 5, theme.role.faint);
    }
  }
  /* Overlay: matches vum_draw_overlay() exactly for the golden harness's ui_sec (static, never advances) -- with ui_sec frozen,
   * "!force && ui_sec == last_sec" is true on every non-force frame, so the C function redraws only once, on force, regardless of
   * INFO (the INFO-off branch also does one unconditional fb_rect(x0,y0,w,h,bg) before returning). One clear rect either way; the
   * INFO-on demo caption is an uncounted visual-only placeholder (see header note). */
  function drawOverlay(fb, x0, y0, w, h, bg, p) {
    fb.rect(x0, y0, w, h, bg);
    if (p.info) fb._fill(x0 + 2, y0 + 3, Math.min(w - 4, 140), 6, fb.px[0]);   // faint demo-caption block, visual only, not logged
  }
  function tick(ctx) {
    const { fb, x: x0, y, w, h, bg, theme, p, st, vu } = ctx;
    const force = !!ctx.force;
    if (force) { st.litDrawn[0] = st.litDrawn[1] = 0xFF; st.peakDrawn[0] = st.peakDrawn[1] = 0xFF; drawFace(fb, x0, y, w, bg, theme); }
    const lx = x0 + VUM_LABEL_W, lw = w > VUM_LABEL_W ? w - VUM_LABEL_W : 1;
    const peakL = (vu && vu.l) || 0, peakR = (vu && vu.r) || 0;
    drawLadder(fb, 0, lx, y, lw, bg, peakL, theme, p, st, force);
    drawLadder(fb, 1, lx, y + VUM_ROW2_Y, lw, bg, peakR, theme, p, st, force);
    if (h > VUM_OVERLAY_Y && force) drawOverlay(fb, x0, y + VUM_OVERLAY_Y, w, h - VUM_OVERLAY_Y, bg, p);
  }
  const api = { key: 'vu_master', state, tick, peakToSegments, coalesce };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).vu_master = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
