/* winamp_bars: a line-for-line port of wviz_bars_tick() in fw/player.c on the shared core twin. ctx = {fb, x, y, w, h, bg, theme, spec,
 * paused, p (params by key), st (module state), force}. Returns nothing; draws through fb.bar / fb.rect and the theme roles. */
(function (root) {
  const C = (typeof module !== 'undefined') ? require('../tau_core.js') : root.TauCore;
  function state(nb) { return { disp: new Array(16).fill(0), vel: Array.from({ length: 16 }, () => ({ v: 0 })), pk: Array.from({ length: 16 }, () => C.newPeak()), drawn: { a: new Array(16).fill(0), b: new Array(16).fill(0) } }; }
  function tick(ctx) {
    const { fb, x: x0, y, w, h, bg, theme, spec, p, st } = ctx;
    let bands = p.bands; if (bands < 4) bands = 4; if (bands > 16) bands = 16;
    const gap = 2, colw = (w > gap * (bands - 1)) ? Math.trunc((w - gap * (bands - 1)) / bands) : 1;
    if (ctx.force) fb.rect(x0, y, w, h, bg);
    const pcfg = { on: p.peak_on, gravity: p.peak_gravity, hold_ms: p.peak_hold_ms, fall: p.peak_fall };
    for (let b = 0; b < bands; b++) {
      let target = C.bandTarget(spec, 16, bands, b);
      if (ctx.paused) target = 0;
      const rate = target >= st.disp[b] ? p.attack : p.release;
      st.disp[b] = C.ease(st.disp[b], target, p.ease, rate, st.vel[b]);
      C.peakStep(st.pk[b], st.disp[b], pcfg, 26);
      if (!C.delta(st.drawn, b, st.disp[b], st.pk[b].peak, ctx.force)) continue;
      const x = x0 + b * (colw + gap);
      let bh = Math.trunc(st.disp[b] * h / 255); if (bh < 2) bh = 2;
      fb.bar(x, y, colw, h, bh, theme.accent, bg);
      if (p.peak_on) { const ph = Math.trunc(st.pk[b].peak * h / 255); if (ph > bh + 1 && ph < h) fb.rect(x, y + h - ph, colw, 1, theme.role.text_primary); }
    }
  }
  const api = { key: 'winamp_bars', state, tick };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).winamp_bars = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
