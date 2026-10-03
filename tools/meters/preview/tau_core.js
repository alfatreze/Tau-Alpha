/* JS twin of fw/meter_core.h (M1.5). Same integer arithmetic as the C: C division truncates toward zero, so every / uses Math.trunc.
 * Checked against the C core's golden vectors (fixtures/core_vectors.json) by test.js. */
(function (root) {
  const tdiv = (a, b) => Math.trunc(a / b) + 0;   // + 0 turns a negative zero into 0
  function ease(cur, target, mode, rate, vel) {          // vel: {v} holder; returns the new value (0..255)
    if (mode === 0) return target;
    if (mode === 1) {
      const step = 1 + tdiv(rate, 6);
      let d = target - cur;
      if (d > step) d = step; else if (d < -step) d = -step;
      return (cur + d) & 255;
    }
    if (mode === 2) {
      const k = 8 + tdiv(rate * 248, 100);
      const d = tdiv((target - cur) * k, 256);
      return (cur + d) & 255;
    }
    const stiff = 6 + tdiv(rate * 58, 100);
    const accel = tdiv((target - cur) * stiff, 256) - tdiv(vel.v * stiff, 512);
    let v = vel.v + accel;
    if (v > 60) v = 60; else if (v < -60) v = -60;
    vel.v = v;
    let p = cur + v;
    if (p < 0) p = 0; else if (p > 255) p = 255;
    return p & 255;
  }
  function newPeak() { return { peak: 0, vel: 0, hold: 0 }; }
  function peakStep(p, disp, c, dtMs) {                 // c: {on, gravity, hold_ms, fall}
    if (!c.on) { p.peak = disp; return; }
    if (disp >= p.peak) { p.peak = disp; p.hold = c.hold_ms; p.vel = 0; }
    else if (p.hold > 0) { p.hold = p.hold > dtMs ? p.hold - dtMs : 0; }
    else {
      let fall;
      if (c.gravity) {
        const nv = p.vel + 1 + tdiv(c.fall, 20);
        p.vel = nv > 255 ? 255 : nv;
        fall = 1 + (p.vel >> 3);
      } else fall = 1 + tdiv(c.fall, 12);
      p.peak = p.peak > fall ? p.peak - fall : 0;
      if (p.peak < disp) p.peak = disp;
    }
  }
  function bandTarget(spec, nspec, bands, b) {
    const lo = tdiv(b * nspec, bands);
    let hi = tdiv((b + 1) * nspec, bands);
    if (hi <= lo) hi = lo + 1;
    let sum = 0, n = 0;
    for (let k = lo; k < hi && k < nspec; k++) { sum += spec[k]; n++; }
    return n ? tdiv(sum, n) : 0;
  }
  function delta(drawn, i, a, b, force) {               // drawn: {a:[], b:[]}
    if (!force && a === drawn.a[i] && b === drawn.b[i]) return false;
    drawn.a[i] = a; drawn.b[i] = b; return true;
  }

  /* ---- colour, geometry, cache, signals, derived measurements (the newer half of fw/meter_core.h) ---- */
  function ramp(a, b, t, n) {
    const r = tdiv(((a >> 11) & 31) * (n - t) + ((b >> 11) & 31) * t, n), g = tdiv(((a >> 5) & 63) * (n - t) + ((b >> 5) & 63) * t, n), bl = tdiv((a & 31) * (n - t) + (b & 31) * t, n);
    return (r << 11) | (g << 5) | bl;
  }
  function mix256(a, b, t) {                            // >> is an arithmetic shift, like the C's on a negative product
    const ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31, br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    return ((ar + (((br - ar) * t) >> 8)) << 11) | ((ag + (((bg - ag) * t) >> 8)) << 5) | (ab + (((bb - ab) * t) >> 8));
  }
  function ladder(lo, mid, hi, r, n) { const half = tdiv(n, 2); return r < half ? ramp(lo, mid, r, half) : ramp(mid, hi, r - half, n - half); }
  function colSpan(x0, w, n, i, gap) { const a = x0 + tdiv(i * w, n), b = x0 + tdiv((i + 1) * w, n); return [a, b - a > gap ? b - a - gap : 1]; }
  function colCw(x0, w, n, i) { const a = x0 + tdiv(i * w, n), b = x0 + tdiv((i + 1) * w, n); return [a, b > a ? b - a : 1]; }
  function scaleU(v, h, full) { const a = tdiv(v * h, full); return a > h ? h : a; }
  function scaleS(v, ey, unit) { let r = tdiv(v * ey, unit); if (r > ey) r = ey; if (r < -ey) r = -ey; return r; }
  function inBox(px, py, sz, x, y, w, h) { return px >= x && px + sz <= x + w && py >= y && py + sz <= y + h ? 1 : 0; }
  const STALE = 255;
  function delta1(drawn, i, v, force) { if (!force && v === drawn[i]) return false; drawn[i] = v; return true; }
  function invalidate(a, n) { for (let i = 0; i < n; i++) a[i] = STALE; }
  function energy(lvl, n) { let s = 0; for (let b = 0; b < n; b++) s += lvl[b]; return tdiv(s, n); }
  function silent(lvl, n, peak) { if (peak) return 0; for (let b = 0; b < n; b++) if (lvl[b]) return 0; return 1; }
  function slewPow(w, lvl, n, up, dn) {
    for (let b = 0; b < n; b++) { const t = (lvl[b] * lvl[b]) >> 4; let d = t - w[b]; if (d > up) d = up; if (d < -dn) d = -dn; w[b] += d; }
  }
  function emaPow(e, lvl, n, div) { for (let b = 0; b < n; b++) e[b] += tdiv(lvl[b] * lvl[b] - e[b], div); }
  function onsetFlux(prev, ema, lvl, n, sens, elapsed, refr) {   // ema: {v}; returns 1 on a trigger
    let rise = 0;
    for (let b = 0; b < n; b++) if (lvl[b] > prev[b]) rise += lvl[b] - prev[b];
    const thr = (Math.floor((sens * ema.v) / 4096)) + 6, fire = rise > thr && elapsed > refr ? 1 : 0;
    const d = ((rise << 8) - ema.v) | 0;                 // the C does this in uint32, casts to int32, shifts arithmetically
    ema.v = (ema.v + (d >> 5)) >>> 0;
    return fire;
  }
  function isqrt32(v) { return Math.floor(Math.sqrt(v)); }                // exact below 2^53
  function isqrt64(v) {                                  // BigInt bit method, like the C
    let n = BigInt(v), r = 0n, bit = 1n << 62n;
    while (bit > n) bit >>= 2n;
    while (bit) { if (n >= r + bit) { n -= r + bit; r = (r >> 1n) + bit; } else r >>= 1n; bit >>= 2n; }
    return Number(r);
  }
  function rms(sum, winLog2) { return isqrt32(Number((BigInt(sum) >> BigInt(winLog2)) & 0xFFFFFFFFn)); }
  function corrQ8(ll, rr, lr) {
    if (!ll || !rr) return 0;
    let L = BigInt(ll), R = BigInt(rr), sh = 0n;
    while ((L >> sh) >= 0x80000000n || (R >> sh) >= 0x80000000n) sh++;
    const neg = lr < 0, m = BigInt(neg ? -lr : lr) >> sh, den = BigInt(isqrt64((L >> sh) * (R >> sh)));
    if (!den) return 0;
    let q = (m * 256n) / den; if (q > 256n) q = 256n;
    return neg && q ? -Number(q) : Number(q);
  }
  function crestQ8(peak, rms_) { if (!rms_) return 0; const q = tdiv(peak * 256, rms_); return q > 0xFFFF ? 0xFFFF : q; }
  function centroidQ8(lvl, n) { let s = 0, num = 0; for (let b = 0; b < n; b++) { s += lvl[b]; num += b * lvl[b]; } return s ? tdiv(num * 256, s) : 0; }
  const api = { ease, newPeak, peakStep, bandTarget, delta, ramp, mix256, ladder, colSpan, colCw, scaleU, scaleS, inBox, STALE, delta1, invalidate, energy, silent, slewPow, emaPow, onsetFlux, isqrt32, isqrt64, rms, corrQ8, crestQ8, centroidQ8 };
  if (typeof module !== 'undefined') module.exports = api; else root.TauCore = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
