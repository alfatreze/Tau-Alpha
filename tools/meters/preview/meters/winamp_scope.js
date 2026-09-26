/* winamp_scope: port of wviz_scope_tick()'s software path (the 64-column path that runs since B-302). */
(function (root) {
  function state() { return { y: new Array(64).fill(0), init: false }; }
  function tick(ctx) {
    const { fb, x: x0, y, w, h, bg, theme, p, st } = ctx;
    const ey = Math.trunc(h / 2) - 1, cy = y + Math.trunc(h / 2);
    if (ctx.force) st.init = false;
    fb.rect(x0, y, w, h, bg); fb.rect(x0, cy, w, 1, theme.role.surface_track);
    if (ctx.paused) return;
    const smooth = p.scope_smooth; let prev = 0;
    for (let c = 0; c < 64; c++) {
      const cx = x0 + Math.trunc(c * w / 64), cxn = x0 + Math.trunc((c + 1) * w / 64), cw = cxn > cx ? cxn - cx : 1;
      let raw = Math.trunc(ctx.wave[c] * ey / 100); if (raw > ey) raw = ey; if (raw < -ey) raw = -ey;
      if (!st.init) st.y[c] = raw;
      st.y[c] = st.y[c] + Math.trunc((raw - st.y[c]) * (100 - smooth) / 100);
      const v = st.y[c], a = c === 0 ? v : prev, lo = a < v ? a : v, hi = a < v ? v : a; prev = v;
      let sTop = cy - hi, sBot = sTop + (hi - lo) + 2;
      if (sTop < y) sTop = y; if (sBot > y + h) sBot = y + h; if (sBot < sTop) sBot = sTop;
      fb.rect(cx, sTop, cw, sBot - sTop, theme.accent);
    }
    st.init = true;
  }
  const api = { key: 'winamp_scope', state, tick };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).winamp_scope = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
