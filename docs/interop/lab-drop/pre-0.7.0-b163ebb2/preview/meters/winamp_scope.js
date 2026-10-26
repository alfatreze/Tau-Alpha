/* winamp_scope: the twin of fw2/ui/meter_winamp_scope.c (Player 2). The hardware capture block records the mono mix as 256 columns, each the min..max envelope over two samples; the module draws 64 columns (four
 * hardware columns each) normalised to the PREVIOUS frame's peak, with temporal smoothing and, where the bitstream has the pipelined blend, a trail (the box blended toward its background by (100 - trail) percent
 * instead of wiped). The first call after a repaint only arms the capture. A frame's capture is `ctx.capture` (256 [min, max] pairs of signed 16-bit values); the lab derives one from the demo waveform
 * (capture(wave)), a recorded trace supplies the waveform only. The blend is one lab command here (on the Pocket: four strip blits and a fence); `ctx.fb.blendReady` false = the wipe. */
(function (root) {
  const COLS = 64, HW = 256;
  function state() { return { pk: 1, armed: false, init: false, mid: new Array(COLS).fill(0), stripH: 0, stripBg: -1 }; }
  /* Lab capture from the 64-column demo waveform (-100..100): each hardware column spans two samples of a linear interpolation, scaled to the 16-bit range. */
  function capture(wave) {
    const out = [];
    for (let k = 0; k < HW; k++) {
      const a = wave[k >> 2] * 300, b = wave[Math.min(COLS - 1, (k >> 2) + ((k & 3) >= 2 ? 1 : 0))] * 300;
      out.push([Math.min(a, b), Math.max(a, b)]);
    }
    return out;
  }
  function tick(ctx) {
    const { fb, x: x0, y, w, h, bg, theme, p, st } = ctx;
    const ey = Math.trunc(h / 2) - 1, cy = y + Math.trunc(h / 2);
    if (ctx.force) { st.init = false; st.armed = false; fb.rect(x0, y, w, h, bg); st.stripH = 0; }
    if (!st.armed) { st.armed = true; return; }
    if (ctx.paused) { st.init = false; return; }
    const trail = p.scope_trail, smooth = p.scope_smooth, cap = ctx.capture || capture(ctx.wave);
    if (trail && fb.blendReady) {
      st.stripH = h; st.stripBg = bg;
      let a = Math.trunc((100 - trail) * 256 / 100); if (a < 1) a = 1; if (a > 255) a = 255;
      fb.blend(x0, y, w, h, bg, 0, a);
    } else fb.rect(x0, y, w, h, bg);
    fb.rect(x0, cy, w, 1, theme.role.surface_track);
    let npk = 1;
    for (let c = 0; c < COLS; c++) {
      let mn = 32767, mx = -32768;
      for (let k = 0; k < HW / COLS; k++) { const d = cap[c * (HW / COLS) + k]; if (d[0] < mn) mn = d[0]; if (d[1] > mx) mx = d[1]; }
      const am = mn < 0 ? -mn : mn, ax = mx < 0 ? -mx : mx;
      if (am > npk) npk = am; if (ax > npk) npk = ax;
      let lo = Math.trunc(mn * ey / st.pk), hi = Math.trunc(mx * ey / st.pk);
      if (lo < -ey) lo = -ey; else if (lo > ey) lo = ey;
      if (hi < -ey) hi = -ey; else if (hi > ey) hi = ey;
      if (lo > hi) lo = hi;
      let mid = Math.trunc((lo + hi) / 2); const half = Math.trunc((hi - lo) / 2);
      if (!st.init) st.mid[c] = mid;
      st.mid[c] = st.mid[c] + Math.trunc(((mid - st.mid[c]) * (100 - smooth)) / 100);
      mid = st.mid[c];
      const cx = x0 + Math.trunc(c * w / COLS), cxn = x0 + Math.trunc((c + 1) * w / COLS), cw = cxn > cx ? cxn - cx : 1;
      let sTop = cy - (mid + half), sBot = sTop + 2 * half + 2;
      if (sTop < y) sTop = y; if (sBot > y + h) sBot = y + h; if (sBot < sTop) sBot = sTop;
      fb.rect(cx, sTop, cw, sBot - sTop, theme.accent);
    }
    st.pk = npk; st.init = true;
  }
  const api = { key: 'winamp_scope', state, tick, capture };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).winamp_scope = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
