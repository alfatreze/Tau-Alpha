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
  const SPLIT = { OCTAVES: 0, BASS_FINE: 1, ENERGY: 2, DYNAMICS: 3, CUSTOM: 4 };
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
  const xoOf = (p) => [p.xo1, p.xo2, p.xo3, p.xo4, p.xo5];
  function boundsFor(split, n, st, xo) {   // xo: the CUSTOM split's boundaries (band index 1..15 between layer i and i+1)
    const b = [0];
    if (split === SPLIT.CUSTOM && xo) {
      for (let i = 1; i < n; i++) b.push(xo[i - 1]);
    } else if (split === SPLIT.BASS_FINE) {       // group widths grow x1.6 with frequency: bass gets the finest layers
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
    const b = boundsFor(p.split, n, st, xoOf(p));
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

  /* Lab-only experiments (NOT in the manifest, not in the firmware): per-layer gain g1..g6 in dB and the SPECTRUM style hstyle. With the defaults
     (0 dB, BLOCKS) every operation below is skipped, so the golden-frame comparison with the firmware is unaffected. */
  const gm = (p, k) => { const db = p['g' + (k + 1)] | 0; return db ? Math.round(256 * Math.pow(10, db / 20)) : 256; };
  /* Antialiasing (lab-only experiment, software edge pixels): the row just outside each layer's edge is painted with the layer colour mixed over what is
     already there by the sub-pixel coverage, quantised to AA_LEVELS[p.aa] steps so neighbouring columns can still merge. One extra command per run and
     edge, so it costs commands (and the self-scaling stride then coarsens the cells): see the Info cost table. */
  const AA_LEVELS = [0, 2, 4, 8];
  const halfPxAA = (a8, w8, Hh, L) => { const v = a8 * w8 * Hh / 65025; let h = Math.floor(v), q = Math.round((v - h) * L); if (q >= L) { h++; q = 0; } if (h < 1 && w8 > 64) { h = 1; q = 0; } return [h, q]; };
  function aaPass(ctx, st, cy, rect, L) {
    const fb = ctx.fb, X = ctx.x, y0 = ctx.y, y1 = ctx.y + ctx.h;
    for (const [x0, x1, h, cv, c] of st.aaRuns) {
      if (!cv) continue;
      for (const y of [cy - h - 1, cy + h]) {
        if (y < y0 || y >= y1) continue;
        let a = x0, cur = fb.px[y * fb.w + X + x0];
        for (let x = x0 + 1; x <= x1; x++) {
          const u = x < x1 ? fb.px[y * fb.w + X + x] : -1;
          if (u !== cur) { rect(X + a, y, x - a, 1, mix(cur, c, tdiv(cv * 256, L))); a = x; cur = u; }
        }
      }
    }
  }
  const LAB_PARAMS = [
    { key: 'guard', label: 'Cost guard', type: 'enum', values: ['ON', 'RELAXED', 'OFF'], default: 0, group: 'lab',
      help: 'The self-scaling guard keeps a frame near 300 drawing commands by widening the drawing cells (lab-only control; the firmware always uses ON).',
      value_help: ['ON: budget 300 commands per frame. With several layers at high Resolution the cells are widened (see the cell width in Info), so edges and the taper step at that width.', 'RELAXED: budget 600 commands; finer cells, about twice the drawing work.', 'OFF: never widen; the full Resolution you asked for, at whatever it costs (can be well over the budget).'] },
    { key: 'aa', label: 'Antialiasing', type: 'enum', values: ['OFF', 'EDGE 2', 'EDGE 4', 'EDGE 8'], default: 0, group: 'lab',
      help: 'Soften the stair-steps along each layer edge with partly covered pixels (lab-only experiment, software). The number is how many coverage levels are used: more levels look smoother but merge less, so they need more drawing commands (see the Info cost table).',
      value_help: ['OFF: whole pixels only (what the firmware does).', 'EDGE 2: one extra pixel row per edge, half or no coverage.', 'EDGE 4: four coverage levels.', 'EDGE 8: eight levels, the smoothest and the most commands.'] },
    { key: 'hstyle', label: 'Spectrum style', type: 'enum', values: ['BLOCKS', 'EQ BELLS', 'CURVE'], default: 0, group: 'lab', when: { view: 1 },
      help: 'How the layers share the frequency axis in SPECTRUM view (lab-only experiment).',
      value_help: ['BLOCKS: hard blocks, each layer draws only its own frequency range (what the firmware does).', 'EQ BELLS: the same ranges with soft shoulders that taper into the neighbours, like the bell curves of a parametric EQ.', 'CURVE: one continuous gradient-coloured outline across the whole axis; the layer colours become colour stops and the layer gains a smooth gain curve.'] },
    { key: 'bmode', label: 'Layer blending', type: 'enum', values: ['OFF', 'ALPHA', 'AVERAGE', 'ADD', 'SUBTRACT', 'ADD QUARTER'], default: 0, group: 'lab',
      help: 'Composite the layers through the hardware blend (B5, lab-only experiment) instead of painting them opaque. Each layer is blended over what is already there, outermost first, so overlapping layers mix. On hardware each run is one blit from a one-row colour strip, so the command count is unchanged.',
      value_help: ['OFF: opaque layers (what the firmware does).', 'ALPHA: ordinary translucency, 0-255 alpha (the DSP path).', 'AVERAGE: half of each, B/2 + F/2.', 'ADD: layer added to what is underneath, clamped (glow, light on dark).', 'SUBTRACT: layer subtracted from what is underneath, clamped to black.', 'ADD QUARTER: a quarter of the layer added, clamped (a faint glow).'] },
    { key: 'balpha', label: 'Blend alpha', type: 'u8', min: 5, max: 100, step: 5, default: 60, unit: '%', group: 'lab', when: { bmode: 1 }, help: 'Opacity of each layer in ALPHA blending.' },
  ].concat([1, 2, 3, 4, 5, 6].map((i) => ({ key: 'g' + i, label: 'Gain, layer ' + i, type: 'u8', min: -18, max: 18, step: 1, default: 0, unit: ' dB', group: 'lab',
    help: 'Gain of layer ' + i + ' in dB (lab-only experiment). Also set by dragging the bead on the frequency strip up or down; double-click the bead to reset.' })));

  /* ---- state ----------------------------------------------------------------------------------------------------------------------- */
  function state() { return { init: false, key: '' }; }
  function reinit(st, ctx, hard) {
    const p = ctx.p, n = p.layers;
    st.hard = hard; st.init = true; st.s = new Array(n).fill(0); st.acc = 0; st.dirty = true;
    st.hist = Array.from({ length: n }, () => new Uint8Array(p.res + 4));
    st.band = Array.from({ length: n }, () => new Array(NB).fill(0));
    st.e = new Array(NB).fill(0); st.eTick = 0; st.ebounds = null;
    st.stride = 1 + tdiv(n * ctx.w, 900); st.cnt = 0;
  }
  function tick(ctx) {
    const p = ctx.p, st = ctx.st, fb = ctx.fb, W = ctx.w, H = ctx.h, X = ctx.x, Y = ctx.y, dt = ctx.dt || 26;
    const cl = colours(ctx);
    // Only the history/band array sizes and the box geometry force a reset; every other change (view, draw, split, colours, ...) just repaints on the next
    // frame and keeps the history, so switching a setting never wipes the picture.
    const hard = [p.layers, p.res, W, H, X, Y].join(','), soft = [p.view, p.draw, p.split, p.outer, p.nest, p.taper, cl.co, cl.ci, cl.bgc, p.aa | 0, p.bmode | 0].join(',');
    if (!st.init || st.hard !== hard || ctx.force) { reinit(st, ctx, hard); fb.rect(X, Y, W, H, cl.bgc); }   // the solid background: ONE command
    else if (st.soft !== soft) st.dirty = true;
    st.soft = soft;
    if (ctx.paused) return;                // the firmware skips drawing while paused: the picture holds
    const cy = Y + (H >> 1), Hh = (H >> 1) - 1;
    st.cnt = 0; st.aaRuns = [];
    if ((p.guard | 0) === 2) st.stride = 1;   // guard off: full resolution from the first frame
    const rect = (x, y, w, h, c) => { fb.rect(x, y, w, h, c); st.cnt++; };
    const bm = p.bmode | 0, ba = Math.round((p.balpha === undefined ? 60 : p.balpha) * 255 / 100);   // lab-only blend experiment: layers composited through the hardware blend instead of opaque
    const lrect = bm ? (x, y, w, h, c) => { fb.blend(x, y, w, h, c, bm - 1, ba); st.cnt++; } : rect;
    if (p.view === 1) spectrum(ctx, st, dt, cl, cy, Hh, rect, lrect); else history(ctx, st, dt, cl, cy, Hh, rect, lrect);
    if (AA_LEVELS[p.aa | 0] && st.aaRuns.length) aaPass(ctx, st, cy, rect, AA_LEVELS[p.aa | 0]);
    const bud = [BUDGET, 600, Infinity][p.guard | 0];   // lab-only: the guard's budget (the firmware always uses BUDGET)
    if (bud === Infinity) st.stride = 1;
    else if (st.cnt) st.stride = st.cnt > bud ? Math.min(16, st.stride + 1) : (st.cnt < (bud >> 1) && st.stride > 1 ? st.stride - 1 : st.stride);
  }

  /* HISTORY view: x is time (newest left); the box is cleared and every layer drawn outermost first, merging neighbouring cells of equal height
     and colour. `stride` widens the evaluation cell when the last frame needed more than BUDGET commands. */
  function history(ctx, st, dt, cl, cy, Hh, rect, lrect) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, res = p.res, smooth = p.draw === DRAW.SMOOTH, aaL = AA_LEVELS[p.aa | 0];
    const tg = targets(p, ctx.spec, ctx.wave, st);
    for (let k = 0; k < n; k++) {
      const m = p.split === SPLIT.DYNAMICS && n > 1 ? T.mulQ8[n - 1 - 2 * k + 5] : 256;
      const gk = gm(p, k), tk = gk === 256 ? tg[k] : Math.min(4096, (tg[k] * gk) >> 8);
      st.s[k] = stepTo(st.s[k], tk, coef(p.response, m, dt, 1), coef(p.response, m, dt, 0));
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
      const flush = () => { if (run) { if (aaL) st.aaRuns.push([run.x0, run.x1, run.h, run.cv, cl.tbl[k][run.q]]); lrect(X + run.x0, cy - run.h, run.x1 - run.x0, 2 * run.h, cl.tbl[k][run.q]); run = null; } };
      for (let xs = 0; xs < W; xs += s) {
        const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
        let a8;
        if (smooth) { const t = tdiv((2 * xc + 1) * res * 128, W) - phase - 128; a8 = t <= 0 ? h[0] : cr8((j) => h[j], res + 4, t >> 8, t & 255); }
        else a8 = h[Math.min(res - 1, tdiv(xc * res, W))];
        const w8 = tq === 0 ? 255 : win8(T.winHist, tq, tdiv((2 * xc + 1) * 8192, W)), aaT = aaL ? halfPxAA(a8, w8, Hh, aaL) : null, hh = aaT ? aaT[0] : halfPx(a8, w8, Hh), cv = aaT ? aaT[1] : 0, q = Math.min(AGE - 1, tdiv((2 * xc + 1) * 4, W));
        if (run && run.h === hh && run.q === q && run.cv === cv) { run.x1 = xs + wd; continue; }
        flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh, q, cv };
      }
      flush();
    }
    if (p.taper > 0) for (let j = 0; j < DOTS.length; j++) { const d = DOTS[j]; rect(X + W - 8 - j * 12 - (d >> 1), cy - (d >> 1), d, d, cl.tbl[n - 1][AGE - 1]); }
  }

  /* SPECTRUM view (no scrolling): x is frequency (bass left), the 16 band levels are the outline. With a frequency split every layer draws only the
     part of the axis it listens to (the same ranges as the handles); with DYNAMICS every layer is the whole outline. Layers also differ by response
     time (outer slow, inner fast) and height. Redrawn every frame. */
  function spectrum(ctx, st, dt, cl, cy, Hh, rect, lrect) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, smooth = p.draw === DRAW.SMOOTH, tq = tdiv(p.taper + 2, 5), aaL = AA_LEVELS[p.aa | 0], s = Math.max(st.stride, tdiv(W + p.res - 1, p.res));   // Resolution = columns across the width (cell width), never finer than the self-scaling stride allows
    rect(X, ctx.y, W, ctx.h, cl.bgc);
    if (p.split === SPLIT.ENERGY) learnEnergy(st, ctx.spec, n);
    const bnd = p.split === SPLIT.DYNAMICS ? null : boundsFor(p.split, n, st, xoOf(p));   // frequency splits: each layer draws only the part of the axis it listens to
    const hstyle = p.hstyle | 0;
    if (hstyle === 2 && bnd) return curveOutline(ctx, st, dt, cl, cy, Hh, rect, bnd, lrect);
    for (let k = 0; k < n; k++) {
      let lo = 0, hi = NB;
      if (bnd) { if (p.nest === 0) { lo = p.outer === 0 ? bnd[k] : 0; hi = p.outer === 0 ? NB : bnd[n - k]; } else { const g = p.outer === 0 ? k : n - 1 - k; lo = bnd[g]; hi = bnd[g + 1]; } }
      const m = n > 1 ? T.mulQ8[n - 1 - 2 * k + 5] : 256, ca = coef(p.response, m, dt, 1), cr = coef(p.response, m, dt, 0), sc = n === 1 ? 1024 : 1024 - tdiv(512 * k, n - 1), b = st.band[k], bu = new Array(NB);
      const gk = gm(p, k);
      for (let i = 0; i < NB; i++) {
        const v = tdiv(ctx.spec[i] * 4096, 255); b[i] = stepTo(b[i], v, ca, cr);
        const wt = hstyle === 1 && bnd ? (i >= lo && i < hi ? 256 : (i === lo - 1 || i === hi) ? 128 : (i === lo - 2 || i === hi + 1) ? 40 : 0) : (i >= lo && i < hi ? 256 : 0);
        let a = wt === 256 ? (toU8(b[i]) * sc) >> 10 : wt === 0 ? 0 : (((toU8(b[i]) * sc) >> 10) * wt) >> 8;
        if (gk !== 256) a = Math.min(255, (a * gk) >> 8);
        bu[i] = a;
      }
      let run = null;
      const flush = () => { if (run) { if (aaL) st.aaRuns.push([run.x0, run.x1, run.h, run.cv, cl.base[k]]); lrect(X + run.x0, cy - run.h, run.x1 - run.x0, 2 * run.h, cl.base[k]); run = null; } };
      for (let xs = 0; xs < W; xs += s) {
        const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
        let a8;
        if (smooth) { const t = tdiv((2 * xc + 1) * NB * 128, W) - 128; a8 = t <= 0 ? bu[0] : cr8((j) => bu[j], NB, t >> 8, t & 255); }
        else a8 = bu[Math.min(NB - 1, tdiv(xc * NB, W))];
        const w8 = tq === 0 ? 255 : win8(T.winSpec, tq, tdiv((2 * xc + 1) * 8192, W)), aaT = aaL ? halfPxAA(a8, w8, Hh, aaL) : null, hh = aaT ? aaT[0] : halfPx(a8, w8, Hh), cv = aaT ? aaT[1] : 0;
        if (run && run.h === hh && run.cv === cv) { run.x1 = xs + wd; continue; }
        flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh, cv };
      }
      flush();
    }
  }

  /* CURVE style (lab-only): ONE continuous outline across the whole axis, coloured by a gradient whose stops are the layer colours at the centres of
     their frequency ranges, with a smooth gain curve through the per-layer gains. Layer 0's response time drives the bands. */
  function curveOutline(ctx, st, dt, cl, cy, Hh, rect, bnd, lrect) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, smooth = p.draw === DRAW.SMOOTH, tq = tdiv(p.taper + 2, 5), aaL = AA_LEVELS[p.aa | 0], s = Math.max(st.stride, tdiv(W + p.res - 1, p.res));
    const m = n > 1 ? T.mulQ8[n - 1 + 5] : 256, ca = coef(p.response, m, dt, 1), cr = coef(p.response, m, dt, 0), b = st.band[0], bu = new Array(NB);
    const lay = (g) => (p.outer === 0 ? g : n - 1 - g), cen = [], gdb = [], col = [];
    for (let g = 0; g < n; g++) { cen.push((bnd[g] + bnd[g + 1]) / 2); gdb.push(p['g' + (lay(g) + 1)] | 0); col.push(cl.base[lay(g)]); }
    const seg = (pos) => { if (pos <= cen[0]) return [0, 0, 0]; if (pos >= cen[n - 1]) return [n - 1, n - 1, 0]; let g = 0; while (pos > cen[g + 1]) g++; return [g, g + 1, (pos - cen[g]) / (cen[g + 1] - cen[g])]; };
    for (let i = 0; i < NB; i++) {
      const v = tdiv(ctx.spec[i] * 4096, 255); b[i] = stepTo(b[i], v, ca, cr);
      const [g0, g1, f] = seg(i + 0.5), db = gdb[g0] + (gdb[g1] - gdb[g0]) * f;
      bu[i] = Math.min(255, Math.round(toU8(b[i]) * Math.pow(10, db / 20)));
    }
    let run = null;
    const flush = () => { if (run) { if (aaL) st.aaRuns.push([run.x0, run.x1, run.h, run.cv, run.c]); lrect(X + run.x0, cy - run.h, run.x1 - run.x0, 2 * run.h, run.c); run = null; } };
    for (let xs = 0; xs < W; xs += s) {
      const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
      let a8;
      if (smooth) { const t = tdiv((2 * xc + 1) * NB * 128, W) - 128; a8 = t <= 0 ? bu[0] : cr8((j) => bu[j], NB, t >> 8, t & 255); }
      else a8 = bu[Math.min(NB - 1, tdiv(xc * NB, W))];
      const w8 = tq === 0 ? 255 : win8(T.winSpec, tq, tdiv((2 * xc + 1) * 8192, W)), aaT = aaL ? halfPxAA(a8, w8, Hh, aaL) : null, hh = aaT ? aaT[0] : halfPx(a8, w8, Hh), cv = aaT ? aaT[1] : 0;
      const [g0, g1, f] = seg((xc + 0.5) / W * NB), c = mix(col[g0], col[g1], Math.round(f * 256));
      if (run && run.h === hh && run.c === c && run.cv === cv) { run.x1 = xs + wd; continue; }
      flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh, c, cv };
    }
    flush();
  }

  /* Lab helpers: what each layer listens to (Hz from the half-octave bank, band 0 = bass), and the layer colours for the split map. */
  const EDGES = Array.from({ length: NB + 1 }, (_, k) => 93.75 * Math.pow(2, k / 2));
  function bandOwner(p, st) {
    const n = p.layers, own = new Array(NB).fill(0);
    if (p.split === SPLIT.DYNAMICS || p.view === 1) return own;
    const b = boundsFor(p.split, n, st || {}, xoOf(p));
    for (let g = 0; g < n; g++) for (let k = b[g]; k < b[g + 1]; k++) own[k] = p.outer === 0 ? g : n - 1 - g;
    return own;
  }
  function describe(p, st) {
    const n = p.layers; if (p.view === 1) return Array.from({ length: n }, (_, k) => ({ layer: k, spectrum: true }));
    if (p.split === SPLIT.DYNAMICS) return Array.from({ length: n }, (_, k) => ({ layer: k, dynamics: true }));
    const b = boundsFor(p.split, n, st || {}, xoOf(p));
    return Array.from({ length: n }, (_, k) => {
      const own = p.outer === 0 ? k : n - 1 - k;
      const lo = p.nest === 0 ? (p.outer === 0 ? b[k] : 0) : b[own], hi = p.nest === 0 ? (p.outer === 0 ? NB : b[n - k]) : b[own + 1];
      return { layer: k, bandLo: lo, bandHi: hi, hzLo: Math.round(EDGES[lo]), hzHi: Math.round(EDGES[hi]) };
    });
  }
  const layerColours = (theme, p) => colours({ p, theme }).base;

  const api = { key: 'layered_wave', state, tick, targets, bandOwner, boundsFor, xoOf, describe, layerColours, ends, EDGES, LAB_PARAMS, SPLIT, DRAW, ROLES, BUDGET };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).layered_wave = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
