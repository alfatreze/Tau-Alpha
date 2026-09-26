/* JS twin of fw/meter_core.h (M1.5). Same integer arithmetic as the C: C division truncates toward zero, so every / uses Math.trunc.
 * Checked against the C core's golden vectors (fixtures/core_vectors.json) by test.js. */
(function (root) {
  const tdiv = (a, b) => Math.trunc(a / b);
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
  const api = { ease, newPeak, peakStep, bandTarget, delta };
  if (typeof module !== 'undefined') module.exports = api; else root.TauCore = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
