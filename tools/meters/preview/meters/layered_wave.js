/* layered_wave: nested, mirrored envelope layers over a scrolling history on a SOLID background (docs/features/meters/LAYERED_WAVE_METER_SPEC.md).
 * ctx = {fb, x, y, w, h, bg, theme, spec[16] (0..255, after the firmware's spectrum ballistics), wave[64] (-100..100), paused, p, st, force, dt}.
 *
 * This is a FLOAT PROTOTYPE: the shape maths use doubles. The firmware port will be fixed point (Q8/Q16) and gets golden frames against this
 * module (METER_MODULE_SPEC.md section 7, M3) before it is selectable. Everything that costs engine commands goes through ctx.fb.rect/copy so the
 * command counts the lab prints are exactly what a firmware port issues.
 *
 * Measurements (hardware first): the 16 half-octave band levels come from tau_spec_bank.sv (spec[]), the broadband peak for the DYNAMICS split
 * comes from tau_wave_meter.sv (wave[] / peak). Nothing here needs RMS, correlation or any new hardware.
 */
(function (root) {
  const F = (typeof module !== 'undefined') ? require('../tau_fb.js') : root.TauFb;
  const NB = 16;
  /* enum order == meters/layered_wave/meter.json "values" order (the persisted/wire value is the index). */
  const SPLIT = { OCTAVES: 0, BASS_FINE: 1, ENERGY: 2, DYNAMICS: 3 };
  const DRAW = { BLOCKS: 0, SMOOTH: 1, SCROLL: 2 };
  const ROLES = ['accent', 'text_primary', 'text_secondary', 'ok', 'warn', 'danger', 'pill', 'error', 'surface', 'surface_track', 'base', 'bg_bottom'];
  const AGE_STEPS = 8;           // colour bands along the width (age); more steps = more distinct colours but fewer mergeable runs
  const SPREAD_DYN = 0.5;        // DYNAMICS split: innermost layer's height as a fraction of the outermost (they would coincide otherwise)
  const DOTS = [4, 3, 3];        // tail dot sizes, drawn when the taper is on (the three dots at the end of the reference image)

  const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
  const ch = (c) => [(c >> 11) & 31, (c >> 5) & 63, c & 31];
  const mix = (a, b, t256) => {                       // integer mix in RGB565 channel space, t256 0..256 = share of b
    const x = ch(a), y = ch(b);
    return ((x[0] + (((y[0] - x[0]) * t256) >> 8)) << 11) | ((x[1] + (((y[1] - x[1]) * t256) >> 8)) << 5) | (x[2] + (((y[2] - x[2]) * t256) >> 8));
  };
  const roleColor = (theme, i) => { const n = ROLES[clamp(i, 0, ROLES.length - 1)]; return n === 'accent' ? theme.accent : theme.role[n]; };

  /* ---- band split ---------------------------------------------------------------------------------------------------------------- */
  function fixBounds(b, n) {                          // strictly increasing, 0 .. NB, at least one band per group
    b[0] = 0; b[n] = NB;
    for (let i = n - 1; i >= 1; i--) b[i] = Math.min(b[i], NB - (n - i));
    for (let i = 1; i < n; i++) b[i] = Math.max(b[i], b[i - 1] + 1);
    return b;
  }
  function boundsFor(split, n, st) {
    const b = [0];
    if (split === SPLIT.BASS_FINE) {                  // group widths grow with frequency (ratio 1.6): bass gets the finest layers
      const w = []; let s = 0; for (let i = 0; i < n; i++) { w.push(Math.pow(1.6, i)); s += w[i]; }
      let acc = 0; for (let i = 1; i <= n; i++) { acc += w[i - 1]; b.push(Math.round(acc * NB / s)); }
    } else if (split === SPLIT.ENERGY && st.ebounds && st.ebounds.length === n + 1) {
      return st.ebounds.slice();
    } else for (let i = 1; i <= n; i++) b.push(Math.round(i * NB / n));   // OCTAVES (and ENERGY before it has learned anything): equal band counts
    return fixBounds(b, n);
  }
  function learnEnergy(st, spec, n) {                 // ENERGY: boundaries at equal cumulative (slowly averaged) energy; recomputed ~4 times a second
    for (let k = 0; k < NB; k++) st.e[k] += ((spec[k] / 255) * (spec[k] / 255) - st.e[k]) / 48;
    if (++st.eTick < 12) return; st.eTick = 0;
    let tot = 0; for (let k = 0; k < NB; k++) tot += st.e[k] + 0.002;
    const b = [0]; let acc = 0, g = 1;
    for (let k = 0; k < NB && g < n; k++) { acc += st.e[k] + 0.002; while (g < n && acc >= tot * g / n) { b.push(k + 1); g++; } }
    while (b.length < n) b.push(NB); b.push(NB);
    st.ebounds = fixBounds(b, n);
  }
  /* power mean of band levels (0..1): with denom = n it is a plain average-like level of the set; with a fixed denom a superset can only be louder
     (nested layers never cross). */
  const pnorm = (spec, lo, hi, denom) => { let s = 0; for (let k = lo; k < hi; k++) { const v = spec[k] / 255; s += v * v * v * v; } return clamp(Math.pow(s / denom, 0.25), 0, 1); };

  /* Per-layer target level 0..1, layer 0 = outermost. */
  function targets(p, spec, wave, st) {
    const n = p.layers, out = new Array(n);
    if (p.split === SPLIT.DYNAMICS) {
      let pk = 0; for (let i = 0; i < wave.length; i++) pk = Math.max(pk, Math.abs(wave[i]));
      const x = clamp(pk / 100 * 1.15, 0, 1);
      for (let k = 0; k < n; k++) out[k] = x * (n === 1 ? 1 : 1 - (1 - SPREAD_DYN) * k / (n - 1));
      return out;
    }
    if (p.split === SPLIT.ENERGY) learnEnergy(st, spec, n);
    const b = boundsFor(p.split, n, st); st.bounds = b;
    for (let k = 0; k < n; k++) {
      if (p.nest === 0) {                             // NESTED: layer k hears its own group plus every group inside it
        const lo = p.outer === 0 ? b[k] : 0, hi = p.outer === 0 ? NB : b[n - k];
        out[k] = pnorm(spec, lo, hi, 4) * (n === 1 ? 1 : 1 - 0.4 * k / (n - 1));   // inner layers sit a little lower so they stay visible inside the outer ones
      } else {                                        // OVERLAP: each layer hears only its own group
        const g = p.outer === 0 ? k : n - 1 - k;
        out[k] = pnorm(spec, b[g], b[g + 1], b[g + 1] - b[g]);
      }
    }
    return out;
  }
  /* Which layer (0 = outermost) each of the 16 bands feeds first, for the lab's split map. */
  function bandOwner(p, st) {
    if (p.view === 1) return Array.from({ length: p.layers }, (_, k) => ({ layer: k, spectrum: true }));
    const n = p.layers, b = p.split === SPLIT.DYNAMICS ? [0, NB] : boundsFor(p.split, n, st || {}), own = new Array(NB).fill(0);
    if (p.split === SPLIT.DYNAMICS) return own;
    for (let g = 0; g < n; g++) for (let k = b[g]; k < b[g + 1]; k++) own[k] = p.outer === 0 ? g : n - 1 - g;
    return own;
  }

  /* ---- shape ---------------------------------------------------------------------------------------------------------------------- */
  const win = (a, taper) => taper === 0 ? 1 : (1 - Math.pow(clamp(a, 0, 1), 1 + 3 * (1 - taper / 100))) * (a < 0.07 ? Math.sin(a / 0.07 * Math.PI / 2) : 1);   // gentle roll-off, pointed head
  function cr(h, t) {                                 // Catmull-Rom through the history samples; t < 0 holds the newest one
    if (t <= 0) return h[0];
    const n = h.length, i = Math.floor(t), f = t - i;
    const p0 = h[Math.max(i - 1, 0)], p1 = h[Math.min(i, n - 1)], p2 = h[Math.min(i + 1, n - 1)], p3 = h[Math.min(i + 2, n - 1)];
    const v = 0.5 * ((2 * p1) + (-p0 + p2) * f + (2 * p0 - 5 * p1 + 4 * p2 - p3) * f * f + (-p0 + 3 * p1 - 3 * p2 + p3) * f * f * f);
    return clamp(v, 0, 1);
  }
  const halfPx = (amp, w, Hh) => { let h = Math.round(amp * w * Hh); if (h < 1 && w > 0.25) h = 1; return h; };   // a thin centre line stays in silence

  /* Accent gradations: outer and inner end colours derived from the (theme-capped) accent. Hue shifts go through HSL on 8-bit channels; the
     firmware does the same once when the meter opens (cold code), never per frame. */
  const rgb8 = (c) => { const x = ch(c); return [Math.round(x[0] * 255 / 31), Math.round(x[1] * 255 / 63), Math.round(x[2] * 255 / 31)]; };
  const pack = (r, g, b) => (Math.round(clamp(r, 0, 255) * 31 / 255) << 11) | (Math.round(clamp(g, 0, 255) * 63 / 255) << 5) | Math.round(clamp(b, 0, 255) * 31 / 255);
  function shiftHue(c, deg) {
    const [r, g, b] = rgb8(c).map((v) => v / 255), mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2, d = mx - mn;
    if (d < 1e-6) return c;                              // greys have no hue: a hue shift is a no-op (a white accent stays white)
    const sat = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    let h = mx === r ? ((g - b) / d + (g < b ? 6 : 0)) : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
    h = ((h * 60 + deg) % 360 + 360) % 360 / 360;
    const q = l < 0.5 ? l * (1 + sat) : l + sat - l * sat, p = 2 * l - q;
    const f = (t) => { t = (t + 1) % 1; return t < 1 / 6 ? p + (q - p) * 6 * t : t < 1 / 2 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p; };
    return pack(f(h + 1 / 3) * 255, f(h) * 255, f(h - 1 / 3) * 255);
  }
  function ends(ctx) {                                   // [outer, inner, background] RGB565 for the chosen colour source
    const p = ctx.p, th = ctx.theme;
    if (p.color_mode === 2) return [p.custom_outer & 0xFFFF, p.custom_inner & 0xFFFF, p.custom_bg & 0xFFFF];
    if (p.color_mode === 1) return [roleColor(th, p.color_outer), roleColor(th, p.color_inner), roleColor(th, p.color_bg)];
    const a = th.accent, bg = mix(th.role.base, a, 36);   // ACCENT: a dark (or, in Light, pale) tint of the accent behind the layers
    const white = 0xFFFF, black = 0;
    switch (p.grad) {
      case 1: return [mix(a, black, 150), a, bg];                           // SHADES
      case 2: return [shiftHue(a, -30), shiftHue(a, 30), bg];               // ANALOGOUS
      case 3: return [a, shiftHue(a, 180), bg];                             // COMPLEMENT
      default: return [a, mix(a, white, 180), bg];                          // TINTS
    }
  }
  function colours(ctx) {
    const p = ctx.p, n = p.layers;
    const [co, ci, bgc] = ends(ctx);
    const base = [], tbl = [];
    for (let k = 0; k < n; k++) {
      base.push(n === 1 ? co : mix(co, ci, Math.round(k * 256 / (n - 1))));
      const row = []; for (let q = 0; q < AGE_STEPS; q++) row.push(mix(base[k], bgc, Math.round((p.taper / 100) * 0.8 * ((q + 0.5) / AGE_STEPS) * 256)));
      tbl.push(row);
    }
    return { base, tbl, bgc };
  }

  /* ---- state ---------------------------------------------------------------------------------------------------------------------- */
  function state() { return { init: false, key: '' }; }
  function reinit(st, ctx, key) {
    const p = ctx.p, n = p.layers;
    st.key = key; st.init = true; st.s = new Array(n).fill(0); st.prev = new Array(n).fill(0); st.acc = 0; st.phase = 0;
    st.hist = Array.from({ length: n }, () => new Float32Array(p.res + 4));
    st.band = Array.from({ length: n }, () => new Array(NB).fill(0)); st.e = new Array(NB).fill(0); st.eTick = 0; st.ebounds = null; st.dirty = true; st.ticks = 0;
  }

  function tick(ctx) {
    const p = ctx.p, st = ctx.st, fb = ctx.fb, W = ctx.w, H = ctx.h, X = ctx.x, Y = ctx.y, dt = ctx.dt || 26, n = p.layers;
    const key = [p.view, p.layers, p.res, p.draw, p.split, p.outer, p.nest, p.color_mode, p.grad, p.color_outer, p.color_inner, p.color_bg, p.custom_outer, p.custom_inner, p.custom_bg, p.taper, W, H, X, Y, ctx.theme.accent, ctx.theme.name, ctx.theme.light].join(',');
    st.work = 0;                                      // column/band evaluations this tick (lab CPU estimate)
    if (!st.init || st.key !== key || ctx.force) {
      reinit(st, ctx, key);
      fb.rect(X, Y, W, H, colours(ctx).bgc);          // the solid background: ONE command (the host's gradient never shows inside the box)
    }
    if (ctx.paused) return;                           // the firmware skips drawing while paused: the picture holds
    const cy = Y + (H >> 1), Hh = (H >> 1) - 1;

    if (p.view === 1) return spectrum(ctx, st, dt, cy, Hh);

    // 1) measure -> per-layer targets -> ballistics (outer layers linger, inner ones follow)
    const tg = targets(p, ctx.spec, ctx.wave, st);
    const tauR = 700 / (1 + p.response / 6), tauA = tauR / 4;
    for (let k = 0; k < n; k++) {
      const m = p.split === SPLIT.DYNAMICS && n > 1 ? Math.pow(1.6, (n - 1) / 2 - k) : 1;   // DYNAMICS: layers differ only by time constant
      const tau = (tg[k] > st.s[k] ? tauA : tauR) * m;
      st.s[k] += (tg[k] - st.s[k]) * (1 - Math.exp(-dt / tau));
    }

    // 2) push cadence: `speed` px/s moves one column of W/res px per push
    const cw = Math.max(1, Math.round(W / p.res)), cwF = W / p.res;
    st.acc += dt * p.speed / 1000 / (p.draw === DRAW.SCROLL ? cw : cwF);
    let pushes = Math.min(Math.floor(st.acc), p.draw === DRAW.SCROLL ? Math.ceil(W / cw) : p.res);
    st.acc -= Math.floor(st.acc);
    const cl = colours(ctx);

    if (p.draw === DRAW.SCROLL) return scroll(ctx, st, cl, pushes, cw, cy, Hh);
    for (let i = 0; i < pushes; i++) for (let k = 0; k < n; k++) { const h = st.hist[k]; h.copyWithin(1, 0, h.length - 1); h[0] = st.s[k]; }
    st.phase = st.acc;
    if (p.draw === DRAW.BLOCKS && pushes === 0 && !st.dirty) return;   // nothing changed on the column grid: no commands at all
    st.dirty = false;
    redraw(ctx, st, cl, cy, Hh, cwF);
  }

  /* SPECTRUM view (no scrolling): x is frequency (bass left), the 16 band levels are the outline, every layer is the same outline with its own
     response time (outer slow, inner fast) and height. Redrawn every frame: BLOCKS = 16 flat columns per layer, otherwise (SMOOTH, SCROLL) a
     Catmull-Rom curve through the bands at every pixel, equal neighbours merged. */
  function spectrum(ctx, st, dt, cy, Hh) {
    const p = ctx.p, fb = ctx.fb, W = ctx.w, X = ctx.x, n = p.layers, cl = colours(ctx), tauR = 700 / (1 + p.response / 6), tauA = tauR / 4;
    fb.rect(X, ctx.y, W, ctx.h, cl.bgc);
    const edge = (a) => p.taper === 0 ? 1 : Math.pow(Math.max(0, Math.sin(Math.PI * clamp(a, 0, 1))), 0.6 * p.taper / 100);
    for (let k = 0; k < n; k++) {
      const m = n > 1 ? Math.pow(1.6, (n - 1) / 2 - k) : 1, sc = n === 1 ? 1 : 1 - SPREAD_DYN * k / (n - 1), b = st.band[k];
      for (let i = 0; i < NB; i++) { const v = ctx.spec[i] / 255, tau = (v > b[i] ? tauA : tauR) * m; b[i] += (v - b[i]) * (1 - Math.exp(-dt / tau)); }
      let run = null;
      const flush = () => { if (run) { fb.rect(X + run.x0, cy - run.h, run.x1 - run.x0, 2 * run.h, cl.base[k]); run = null; } };
      st.work += p.draw === DRAW.BLOCKS ? NB : W;
      for (let x = 0; x < W; x++) {
        const a = (x + 0.5) / W, t = a * NB - 0.5;
        const amp = (p.draw === DRAW.BLOCKS ? b[Math.min(NB - 1, Math.floor(a * NB))] : cr(b, Math.max(t, 0.0001))) * sc;
        const h = halfPx(amp, edge(a), Hh);
        if (run && run.h === h) { run.x1 = x + 1; continue; }
        flush(); if (h > 0) run = { x0: x, x1: x + 1, h };
      }
      flush();
    }
  }

  /* BLOCKS / SMOOTH: clear the box and draw every layer, outermost first, merging neighbouring columns that share height and colour. */
  function redraw(ctx, st, cl, cy, Hh, cwF) {
    const p = ctx.p, fb = ctx.fb, W = ctx.w, X = ctx.x, n = p.layers, smooth = p.draw === DRAW.SMOOTH;
    fb.rect(X, ctx.y, W, ctx.h, cl.bgc);
    for (let k = 0; k < n; k++) {
      st.work += smooth ? W : p.res;
      let run = null;
      const flush = () => { if (run) { fb.rect(X + run.x0, cy - run.h, run.x1 - run.x0, 2 * run.h, cl.tbl[k][run.q]); run = null; } };
      for (let x = 0; x < W; x++) {
        let amp;
        if (smooth) amp = cr(st.hist[k], (x + 0.5) / cwF - st.phase - 0.5);
        else amp = st.hist[k][Math.min(p.res - 1, Math.floor(x / cwF))];
        const a = (x + 0.5) / W, h = halfPx(amp, win(a, p.taper), Hh), q = Math.min(AGE_STEPS - 1, Math.floor(a * AGE_STEPS));
        if (run && run.h === h && run.q === q) { run.x1 = x + 1; continue; }
        flush(); if (h > 0) run = { x0: x, x1: x + 1, h, q };
      }
      flush();
    }
    dots(ctx, cl, cy);
  }
  function dots(ctx, cl, cy) {
    if (ctx.p.taper === 0) return;
    for (let j = 0; j < DOTS.length; j++) { const s = DOTS[j]; ctx.fb.rect(ctx.x + ctx.w - 8 - j * 12 - (s >> 1), cy - (s >> 1), s, s, cl.tbl[ctx.p.layers - 1][AGE_STEPS - 1]); }
  }

  /* SCROLL: the picture IS the history. Shift it right with engine copies (rows are bursts of at most 127 pixels, far strip first so overlapping
     source is read before it is overwritten), clear and draw only the new columns, then carve the tail taper with background wedges. About a tenth
     of the commands of a full redraw at the same resolution, at the price of baked-in colour and a clipped (not scaled) taper. */
  function scroll(ctx, st, cl, pushes, cw, cy, Hh) {
    const p = ctx.p, fb = ctx.fb, W = ctx.w, X = ctx.x, Y = ctx.y, H = ctx.h, n = p.layers;
    if (pushes <= 0) return;
    const shift = Math.min(W, pushes * cw);
    for (let sx = W - shift; sx > 0; sx -= 127) { const w = Math.min(127, sx); fb.copy(X + sx - w, Y, X + sx - w + shift, Y, w, H); }
    fb.rect(X, Y, shift, H, cl.bgc);
    const cols = Math.ceil(shift / cw); st.work += cols * n + 40;
    for (let j = cols - 1; j >= 0; j--) {             // oldest new column first; each blends the previous state into the current one
      const f = (cols - j) / cols;
      for (let k = 0; k < n; k++) {
        const amp = st.prev[k] + (st.s[k] - st.prev[k]) * f, h = halfPx(amp, 1, Hh);
        if (h > 0) fb.rect(X + j * cw, cy - h, cw, 2 * h, cl.base[k]);
      }
    }
    for (let k = 0; k < n; k++) st.prev[k] = st.s[k];
    if (p.taper > 0) {                                // stepped taper mask: stripes widen until the edge has moved 4 rows (fewer commands on shallow slopes)
      let xs = 0;
      while (xs < W) {
        const keep0 = Math.round(Hh * win(Math.min(1, (xs + 2) / W), p.taper));
        let w = 4;
        while (xs + w < W && w < 24 && keep0 - Math.round(Hh * win(Math.min(1, (xs + w + 2) / W), p.taper)) < 4) w += 2;
        w = Math.min(w, W - xs);
        const keep = Math.round(Hh * win(Math.min(1, (xs + w / 2) / W), p.taper)), cut = Hh - keep;
        if (cut > 0 && xs >= cw * 4) { fb.rect(X + xs, cy - Hh, w, cut, cl.bgc); fb.rect(X + xs, cy + keep, w, cut + 1, cl.bgc); }
        xs += w;
      }
      dots(ctx, cl, cy);
    }
  }

  /* Lab helpers: what each layer listens to (Hz from the half-octave bank, band 0 = bass), and the layer colours for the split map. */
  const EDGES = Array.from({ length: NB + 1 }, (_, k) => 93.75 * Math.pow(2, k / 2));
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
  function layerColours(theme, p) { return colours({ p, theme }).base; }
  const api = { ends, describe, layerColours, EDGES, key: 'layered_wave', state, tick, targets, bandOwner, boundsFor, SPLIT, DRAW, ROLES, _win: win };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).layered_wave = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
