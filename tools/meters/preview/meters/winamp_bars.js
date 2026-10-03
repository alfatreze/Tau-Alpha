/* winamp_bars: a line-for-line port of wviz_bars_tick() in fw/player.c on the shared core twin. ctx = {fb, x, y, w, h, bg, theme, spec,
 * paused, p (params by key), st (module state), force}. Returns nothing; draws through fb.bar / fb.rect and the theme roles. */
(function (root) {
  const C = (typeof module !== 'undefined') ? require('../tau_core.js') : root.TauCore;
  function state(nb) { return { disp: new Array(16).fill(0), vel: Array.from({ length: 16 }, () => ({ v: 0 })), pk: Array.from({ length: 16 }, () => C.newPeak()), drawn: { a: new Array(16).fill(0), b: new Array(16).fill(0) }, bh: new Array(16).fill(0), ph: new Array(16).fill(0), geo: new Array(7).fill(0) }; }
  function tick(ctx) {
    const { fb, x: x0, y, w, h, bg, theme, spec, p, st } = ctx;
    let bands = p.bands; if (bands < 4) bands = 4; if (bands > 16) bands = 16;
    const gap = 2, colw = (w > gap * (bands - 1)) ? Math.trunc((w - gap * (bands - 1)) / bands) : 1;
    // Delta repaint: a changed band paints only the rows between its old and new height; anything that makes the screen unreliable repaints everything.
    const geo = [x0, y, w, h, bands, theme.accent, bg];
    let full = !!ctx.force;
    for (let i = 0; i < 7; i++) if (st.geo[i] !== geo[i]) { full = true; st.geo[i] = geo[i]; }
    if (full) fb.rect(x0, y, w, h, bg);
    const pcfg = { on: p.peak_on, gravity: p.peak_gravity, hold_ms: p.peak_hold_ms, fall: p.peak_fall };
    for (let b = 0; b < bands; b++) {
      let target = C.bandTarget(spec, 16, bands, b);
      if (ctx.paused) target = 0;
      const rate = target >= st.disp[b] ? p.attack : p.release;
      st.disp[b] = C.ease(st.disp[b], target, p.ease, rate, st.vel[b]);
      C.peakStep(st.pk[b], st.disp[b], pcfg, 26);
      if (!C.delta(st.drawn, b, st.disp[b], st.pk[b].peak, full)) continue;
      const x = x0 + b * (colw + gap);
      let bh = Math.trunc(st.disp[b] * h / 255); if (bh < 2) bh = 2;
      let pnew = 0;
      if (p.peak_on) { const ph = Math.trunc(st.pk[b].peak * h / 255); if (ph > bh + 1 && ph < h) pnew = ph; }
      const old = st.bh[b], pold = st.ph[b];
      if (full || !old) {
        fb.bar(x, y, colw, h, bh, theme.accent, bg);
        if (pnew) fb.rect(x, y + h - pnew, colw, 1, theme.role.text_primary);
      } else {
        if (bh > old) fb.rect(x, y + h - bh, colw, bh - old, theme.accent);
        else if (bh < old) fb.rect(x, y + h - old, colw, old - bh, bg);
        if (pold !== pnew) {
          if (pold > bh) fb.rect(x, y + h - pold, colw, 1, bg);
          if (pnew) fb.rect(x, y + h - pnew, colw, 1, theme.role.text_primary);
        }
      }
      st.bh[b] = bh; st.ph[b] = pnew;
    }
  }
  const api = { key: 'winamp_bars', state, tick };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).winamp_bars = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
