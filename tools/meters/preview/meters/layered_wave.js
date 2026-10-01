/* layered_wave: nested, mirrored envelope layers on a SOLID background (docs/features/meters/LAYERED_WAVE_METER_SPEC.md).
 * ctx = {fb, x, y, w, h, theme, spec[16] (0..255, after the firmware's spectrum ballistics), wave[64] (-100..100), paused, p, st, force, dt}.
 *
 * INTEGER twin of fw/layered_wave.inc: every operation here is the same integer operation the C does (C division truncates, so every / is
 * Math.trunc; >> is arithmetic; products stay inside 32 bits or go through an exact isqrt), so the two issue the SAME engine commands for the
 * same input (sim/test_layered_wave_golden.py compares them command for command). Floating point exists only in tools/gen_layered_wave_tables.py.
 *
 * Draw modes: BLOCKS (flat columns) and SMOOTH (Catmull-Rom between history points). A scrolling-by-copy mode (SCROLL, one engine copy plus the new
 * columns) was prototyped in commit 419fea5 and parked: it needs the blit_shift capability (overlapping copy), unproven on hardware. A self-scaling
 * stride keeps the commands per frame near BUDGET whatever the layer count or resolution (graceful degradation instead of audio jitter).
 */
(function (root) {
  const T = (typeof module !== 'undefined') ? require('../lw_tables.js') : root.LwTables;
  const NB = 16, AGE = 8, BUDGET = 300;
  /* enum order == meters/layered_wave/meter.json "values" order (the persisted/wire value is the index). */
  const SPLIT = { OCTAVES: 0, BASS_FINE: 1, ENERGY: 2, DYNAMICS: 3 };
  const DRAW = { BLOCKS: 0, SMOOTH: 1 };
  const ROLES = ['accent', 'text_primary', 'text_secondary', 'ok', 'warn', 'danger', 'pill', 'error', 'surface', 'surface_track', 'base', 'bg_bottom'];
  const DOTS = [4, 3, 3];                // tail dot sizes, drawn when the taper is on (the three dots at the end of the reference image)
  const FW = [1000, 1600, 2560, 4096, 6554, 10486];   // BASS_FINE group weights, 1.6^i x 1000

  const tdiv = (a, b) => Math.trunc(a / b);
  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const isqrt = (n) => { let r = Math.floor(Math.sqrt(n)); while (r * r > n) r--; while ((r + 1) * (r + 1) <= n) r++; return r; };
  const ch = (c) => [(c >> 11) & 31, (c >> 5) & 63, c & 31];
  const mix = (a, b, t256) => {            // integer mix in RGB565 channel space, t256 0..256 = share of b
    const x = ch(a), y = ch(b);
    return ((x[0] + (((y[0] - x[0]) * t256) >> 8)) << 11) | ((x[1] + (((y[1] - x[1]) * t256) >> 8)) << 5) | (x[2] + (((y[2] - x[2]) * t256) >> 8));
  };
  const rgb8 = (c) => { const x = ch(c); return [tdiv(x[0] * 255 + 15, 31), tdiv(x[1] * 255 + 31, 63), tdiv(x[2] * 255 + 15, 31)]; };
  const pack = (r, g, b) => (tdiv(r * 31 + 127, 255) << 11) | (tdiv(g * 63 + 127, 255) << 5) | tdiv(b * 31 + 127, 255);
  const roleColor = (theme, i) => { const n = ROLES[clamp(i, 0, ROLES.length - 1)]; return n === 'accent' ? theme.accent : theme.role[n]; };
  const toU8 = (v) => Math.min(255, (v * 255 + 2048) >> 12);          // Q12 level -> 0..255

  /* ---- colours --------------------------------------------------------------------------------------------------------------------- */
  function hueRotate(c, which) {           // which: 0 = -30, 1 = +30, 2 = 180 degrees; grey stays grey
    const [r, g, b] = rgb8(c), m = T.hue[which], o = [];
    for (let i = 0; i < 3; i++) o.push(clamp((m[i * 3] * r + m[i * 3 + 1] * g + m[i * 3 + 2] * b + 512) >> 10, 0, 255));
    return pack(o[0], o[1], o[2]);
  }
  function ends(ctx) {                     // [outer, inner, background] RGB565 for the chosen colour source
    const p = ctx.p, th = ctx.theme;
    if (p.color_mode === 2) return [p.custom_outer & 0xFFFF, p.custom_inner & 0xFFFF, p.custom_bg & 0xFFFF];
    if (p.color_mode === 1) return [roleColor(th, p.color_outer), roleColor(th, p.color_inner), roleColor(th, p.color_bg)];
    const a = th.accent, bg = mix(th.role.base, a, 36);   // ACCENT: a dark (or, in Light, pale) tint of the accent behind the layers
    switch (p.grad) {
      case 1: return [mix(a, 0, 150), a, bg];                              // SHADES
      case 2: return [hueRotate(a, 0), hueRotate(a, 1), bg];               // ANALOGOUS
      case 3: return [a, hueRotate(a, 2), bg];                             // COMPLEMENT
      default: return [a, mix(a, 0xFFFF, 180), bg];                        // TINTS
    }
  }
  function colours(ctx) {
    const p = ctx.p, n = p.layers, [co, ci, bgc] = ends(ctx), base = [], tbl = [];
    for (let k = 0; k < n; k++) {
      base.push(n === 1 ? co : mix(co, ci, tdiv(k * 512 + (n - 1), 2 * (n - 1))));
      const row = []; for (let q = 0; q < AGE; q++) row.push(mix(base[k], bgc, tdiv(p.taper * (2 * q + 1) * 128 + 500, 1000)));
      tbl.push(row);
    }
    return { base, tbl, bgc, co, ci };
  }

  /* ---- band split ------------------------------------------------------------------------------------------------------------------ */
  function fixBounds(b, n) {               // strictly increasing, 0 .. NB, at least one band per group
    b[0] = 0; b[n] = NB;
    for (let i = n - 1; i >= 1; i--) b[i] = Math.min(b[i], NB - (n - i));
    for (let i = 1; i < n; i++) b[i] = Math.max(b[i], b[i - 1] + 1);
    return b;
  }
  function boundsFor(split, n, st) {
    const b = [0];
    if (split === SPLIT.BASS_FINE) {       // group widths grow x1.6 with frequency: bass gets the finest layers
      let s = 0; for (let i = 0; i < n; i++) s += FW[i];
      let acc = 0; for (let i = 1; i <= n; i++) { acc += FW[i - 1]; b.push(tdiv(acc * 32 + s, 2 * s)); }
    } else if (split === SPLIT.ENERGY && st.ebounds && st.ebounds.length === n + 1) {
      return st.ebounds.slice();
    } else for (let i = 1; i <= n; i++) b.push(tdiv(i * 32 + n, 2 * n));       // OCTAVES (and ENERGY before it has learned anything)
    return fixBounds(b, n);
  }
  function learnEnergy(st, spec, n) {      // ENERGY: boundaries at equal cumulative (slowly averaged) energy; recomputed ~4 times a second
    for (let k = 0; k < NB; k++) { const sq = spec[k] * spec[k]; st.e[k] += tdiv(sq - st.e[k], 48); }
    if (++st.eTick < 12) return; st.eTick = 0;
    let tot = 0; for (let k = 0; k < NB; k++) tot += st.e[k] + 130;
    const b = [0]; let acc = 0, g = 1;
    for (let k = 0; k < NB && g < n; k++) { acc += st.e[k] + 130; while (g < n && acc * n >= tot * g) { b.push(k + 1); g++; } }
    while (b.length < n) b.push(NB); b.push(NB);
    st.ebounds = fixBounds(b, n);
  }
  /* Power mean of band levels, 0..4096 (Q12). denom 4 = nested (a superset can only be louder, so layers never cross); denom = group size = overlap. */
  function pnorm(spec, lo, hi, denom) {
    let sum = 0; for (let k = lo; k < hi; k++) { const a = spec[k] * spec[k]; sum += Math.floor(a * a / 65536); }   // a*a can pass 2^31: floor-divide, not >>
    const M = tdiv((sum * 1040) >> 10, denom), s1 = isqrt(M * 65536), s2 = isqrt(s1 * 65536);
    return Math.min(65536, s2) >> 4;
  }
  /* Per-layer target level (Q12), layer 0 = outermost. */
  function targets(p, spec, wave, st) {
    const n = p.layers, out = new Array(n);
    if (p.split === SPLIT.DYNAMICS) {
      let pk = 0; for (let i = 0; i < wave.length; i++) pk = Math.max(pk, Math.abs(wave[i]));
      const x = Math.min(4096, pk * 47);
      for (let k = 0; k < n; k++) out[k] = n === 1 ? x : (x * (1024 - tdiv(512 * k, n - 1))) >> 10;
      return out;
    }
    if (p.split === SPLIT.ENERGY) learnEnergy(st, spec, n);
    const b = boundsFor(p.split, n, st);
    for (let k = 0; k < n; k++) {
      if (p.nest === 0) {                  // NESTED: layer k hears its own group plus every group inside it; inner layers sit a little lower
        const lo = p.outer === 0 ? b[k] : 0, hi = p.outer === 0 ? NB : b[n - k], v = pnorm(spec, lo, hi, 4);
        out[k] = n === 1 ? v : (v * (1024 - tdiv(410 * k, n - 1))) >> 10;
      } else {                             // OVERLAP: each layer hears only its own group
        const g = p.outer === 0 ? k : n - 1 - k;
        out[k] = pnorm(spec, b[g], b[g + 1], b[g + 1] - b[g]);
      }
    }
    return out;
  }

  /* ---- ballistics: coef = 1 - exp(-dt/tau), integer ------------------------------------------------------------------------------ */
  function coef(response, mulQ8, dt, att) {                      // Q16
    let tau = tdiv(4200 * 256, 6 + response);                    // release time constant, ms in Q8
    if (att) tau = tau >> 2;
    tau = (tau * mulQ8) >> 8;
    const y = tdiv(dt * 16777216, tau) >> 3, y2 = Math.floor(y * y / 65536), y3 = Math.floor(y2 * y / 65536), y4 = Math.floor(y3 * y / 65536);
    const D = 65536 + y + (y2 >> 1) + tdiv(y3, 6) + tdiv(y4, 24);
    let E = tdiv(2147483648, D >> 1); if (E > 65535) E = 65535;
    for (let i = 0; i < 3; i++) E = Math.floor(E * E / 65536);   // E*E can pass 2^31
    return 65536 - E;
  }
  const stepTo = (s, t, ca, cr) => s + (((t - s) * (t > s ? ca : cr)) >> 16);

  /* ---- shape ----------------------------------------------------------------------------------------------------------------------- */
  function win8(tab, q, pos) {             // window 0..255 at pos = a*64 in Q8; q = taper_q 1..20
    const i0 = pos >> 8, fr = pos & 255, i1 = Math.min(64, i0 + 1), t = tab[q - 1];
    return t[i0] + (((t[i1] - t[i0]) * fr) >> 8);
  }
  function cr8(h, n, i, f) {               // Catmull-Rom on 0..255 samples (h(j) reads sample j), f in Q8
    const p0 = h(Math.max(i - 1, 0)), p1 = h(Math.min(i, n - 1)), p2 = h(Math.min(i + 1, n - 1)), p3 = h(Math.min(i + 2, n - 1));
    const a = p2 - p0, b = 2 * p0 - 5 * p1 + 4 * p2 - p3, c = -p0 + 3 * p1 - 3 * p2 + p3;
    const t2 = b + ((c * f) >> 8), t1 = a + ((t2 * f) >> 8);
    return clamp(p1 + ((t1 * f) >> 9), 0, 255);
  }
  const halfPx = (a8, w8, Hh) => { let h = tdiv(a8 * w8 * Hh + 32512, 65025); if (h < 1 && w8 > 64) h = 1; return h; };   // a thin centre line stays in silence

  /* ---- state ----------------------------------------------------------------------------------------------------------------------- */
  function state() { return { init: false, key: '' }; }
  function reinit(st, ctx, key) {
    const p = ctx.p, n = p.layers;
    st.key = key; st.init = true; st.s = new Array(n).fill(0); st.acc = 0; st.dirty = true;
    st.hist = Array.from({ length: n }, () => new Uint8Array(p.res + 4));
    st.band = Array.from({ length: n }, () => new Array(NB).fill(0));
    st.e = new Array(NB).fill(0); st.eTick = 0; st.ebounds = null;
    st.stride = 1 + tdiv(n * ctx.w, 900); st.cnt = 0;
  }
  function tick(ctx) {
    const p = ctx.p, st = ctx.st, fb = ctx.fb, W = ctx.w, H = ctx.h, X = ctx.x, Y = ctx.y, dt = ctx.dt || 26;
    const cl = colours(ctx);
    const key = [p.view, p.layers, p.res, p.draw, p.split, p.outer, p.nest, p.taper, W, H, X, Y, cl.co, cl.ci, cl.bgc].join(',');
    if (!st.init || st.key !== key || ctx.force) { reinit(st, ctx, key); fb.rect(X, Y, W, H, cl.bgc); }   // the solid background: ONE command
    if (ctx.paused) return;                // the firmware skips drawing while paused: the picture holds
    const cy = Y + (H >> 1), Hh = (H >> 1) - 1;
    st.cnt = 0;
    const rect = (x, y, w, h, c) => { fb.rect(x, y, w, h, c); st.cnt++; };
    if (p.view === 1) spectrum(ctx, st, dt, cl, cy, Hh, rect); else history(ctx, st, dt, cl, cy, Hh, rect);
    if (st.cnt) st.stride = st.cnt > BUDGET ? Math.min(16, st.stride + 1) : (st.cnt < (BUDGET >> 1) && st.stride > 1 ? st.stride - 1 : st.stride);
  }

  /* HISTORY view: x is time (newest left); the box is cleared and every layer drawn outermost first, merging neighbouring cells of equal height
     and colour. `stride` widens the evaluation cell when the last frame needed more than BUDGET commands. */
  function history(ctx, st, dt, cl, cy, Hh, rect) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, res = p.res, smooth = p.draw === DRAW.SMOOTH;
    const tg = targets(p, ctx.spec, ctx.wave, st);
    for (let k = 0; k < n; k++) {
      const m = p.split === SPLIT.DYNAMICS && n > 1 ? T.mulQ8[n - 1 - 2 * k + 5] : 256;
      st.s[k] = stepTo(st.s[k], tg[k], coef(p.response, m, dt, 1), coef(p.response, m, dt, 0));
    }
    st.acc += tdiv(dt * p.speed * res * 256, 1000 * W);
    const pushes = Math.min(st.acc >> 8, res); st.acc &= 255;
    for (let i = 0; i < pushes; i++) for (let k = 0; k < n; k++) { const h = st.hist[k]; h.copyWithin(1, 0, h.length - 1); h[0] = toU8(st.s[k]); }
    if (!smooth && pushes === 0 && !st.dirty) return;          // nothing changed on the column grid: no commands at all
    st.dirty = false;
    const phase = st.acc, tq = tdiv(p.taper + 2, 5), s = st.stride;
    rect(X, ctx.y, W, ctx.h, cl.bgc);
    for (let k = 0; k < n; k++) {
      const h = st.hist[k]; let run = null;
      const flush = () => { if (run) { rect(X + run.x0, cy - run.h, run.x1 - run.x0, 2 * run.h, cl.tbl[k][run.q]); run = null; } };
      for (let xs = 0; xs < W; xs += s) {
        const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
        let a8;
        if (smooth) { const t = tdiv((2 * xc + 1) * res * 128, W) - phase - 128; a8 = t <= 0 ? h[0] : cr8((j) => h[j], res + 4, t >> 8, t & 255); }
        else a8 = h[Math.min(res - 1, tdiv(xc * res, W))];
        const w8 = tq === 0 ? 255 : win8(T.winHist, tq, tdiv((2 * xc + 1) * 8192, W)), hh = halfPx(a8, w8, Hh), q = Math.min(AGE - 1, tdiv((2 * xc + 1) * 4, W));
        if (run && run.h === hh && run.q === q) { run.x1 = xs + wd; continue; }
        flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh, q };
      }
      flush();
    }
    if (p.taper > 0) for (let j = 0; j < DOTS.length; j++) { const d = DOTS[j]; rect(X + W - 8 - j * 12 - (d >> 1), cy - (d >> 1), d, d, cl.tbl[n - 1][AGE - 1]); }
  }

  /* SPECTRUM view (no scrolling): x is frequency (bass left), the 16 band levels are the outline, every layer is the same outline with its own
     response time (outer slow, inner fast) and height. Redrawn every frame. */
  function spectrum(ctx, st, dt, cl, cy, Hh, rect) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, smooth = p.draw === DRAW.SMOOTH, tq = tdiv(p.taper + 2, 5), s = st.stride;
    rect(X, ctx.y, W, ctx.h, cl.bgc);
    for (let k = 0; k < n; k++) {
      const m = n > 1 ? T.mulQ8[n - 1 - 2 * k + 5] : 256, ca = coef(p.response, m, dt, 1), cr = coef(p.response, m, dt, 0), sc = n === 1 ? 1024 : 1024 - tdiv(512 * k, n - 1), b = st.band[k], bu = new Array(NB);
      for (let i = 0; i < NB; i++) { const v = tdiv(ctx.spec[i] * 4096, 255); b[i] = stepTo(b[i], v, ca, cr); bu[i] = (toU8(b[i]) * sc) >> 10; }
      let run = null;
      const flush = () => { if (run) { rect(X + run.x0, cy - run.h, run.x1 - run.x0, 2 * run.h, cl.base[k]); run = null; } };
      for (let xs = 0; xs < W; xs += s) {
        const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
        let a8;
        if (smooth) { const t = tdiv((2 * xc + 1) * NB * 128, W) - 128; a8 = t <= 0 ? bu[0] : cr8((j) => bu[j], NB, t >> 8, t & 255); }
        else a8 = bu[Math.min(NB - 1, tdiv(xc * NB, W))];
        const w8 = tq === 0 ? 255 : win8(T.winSpec, tq, tdiv((2 * xc + 1) * 8192, W)), hh = halfPx(a8, w8, Hh);
        if (run && run.h === hh) { run.x1 = xs + wd; continue; }
        flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh };
      }
      flush();
    }
  }

  /* Lab helpers: what each layer listens to (Hz from the half-octave bank, band 0 = bass), and the layer colours for the split map. */
  const EDGES = Array.from({ length: NB + 1 }, (_, k) => 93.75 * Math.pow(2, k / 2));
  function bandOwner(p, st) {
    const n = p.layers, own = new Array(NB).fill(0);
    if (p.split === SPLIT.DYNAMICS || p.view === 1) return own;
    const b = boundsFor(p.split, n, st || {});
    for (let g = 0; g < n; g++) for (let k = b[g]; k < b[g + 1]; k++) own[k] = p.outer === 0 ? g : n - 1 - g;
    return own;
  }
  function describe(p, st) {
    const n = p.layers; if (p.view === 1) return Array.from({ length: n }, (_, k) => ({ layer: k, spectrum: true }));
    if (p.split === SPLIT.DYNAMICS) return Array.from({ length: n }, (_, k) => ({ layer: k, dynamics: true }));
    const b = boundsFor(p.split, n, st || {});
    return Array.from({ length: n }, (_, k) => {
      const own = p.outer === 0 ? k : n - 1 - k;
      const lo = p.nest === 0 ? (p.outer === 0 ? b[k] : 0) : b[own], hi = p.nest === 0 ? (p.outer === 0 ? NB : b[n - k]) : b[own + 1];
      return { layer: k, bandLo: lo, bandHi: hi, hzLo: Math.round(EDGES[lo]), hzHi: Math.round(EDGES[hi]) };
    });
  }
  const layerColours = (theme, p) => colours({ p, theme }).base;

  const api = { key: 'layered_wave', state, tick, targets, bandOwner, boundsFor, describe, layerColours, ends, EDGES, SPLIT, DRAW, ROLES, BUDGET };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).layered_wave = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
