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

  /* ---- experimental settings (manifest group "experimental"; compiled into the firmware only in the Diagnostic Build) ---------------------------------
     With every one at its default (0 dB gains, BLOCKS, no antialiasing, no blending, guard ON) every operation below is skipped and the output is the
     plain meter, bit for bit. */
  const gm = (p, k) => T.gainQ8[clamp(p['g' + (k + 1)] | 0, 0, 36)];       // layer gain, Q8 (256 = 0 dB)
  const AA_LEVELS = [0, 2, 4, 8];
  const BUDGETS = [BUDGET, 600, 1000000000];                                      // cost guard ON / RELAXED / OFF
  const halfPxAA = (a8, w8, Hh, L) => { const v = a8 * w8 * Hh; let h = tdiv(v, 65025), q = tdiv((v - h * 65025) * L + 32512, 65025); if (q >= L) { h++; q = 0; } if (h < 1 && w8 > 64) { h = 1; q = 0; } return [h, q]; };
  /* The five hardware blend modes (mp3_fb.sv blend_ch), applied here in software to colours the CPU already knows (no destination read needed). */
  const blendCh = (b, f, mode, alpha, mx) => mode === 0 ? ((f * alpha + b * (256 - alpha)) >> 8) : mode === 1 ? ((b + f) >> 1) : mode === 2 ? Math.min(mx, b + f) : mode === 3 ? (f > b ? 0 : b - f) : Math.min(mx, b + (f >> 2));
  const blendPx = (bg, fg, mode, alpha) => (blendCh(bg >> 11, fg >> 11, mode, alpha, 31) << 11) | (blendCh((bg >> 5) & 63, (fg >> 5) & 63, mode, alpha, 63) << 5) | blendCh(bg & 31, fg & 31, mode, alpha, 31);

  /* Per-frame context for the layer functions: antialiasing level, blend mode and alpha, and the per-column memory of the layer drawn just before
     (height st.ph, final colour st.pc), which blending and the antialiasing edge colour both need. Layer k's own values go to st.nh/st.nc and replace
     ph/pc when the layer is done. Layers draw outermost first. */
  function cellOut(lay, k, xs, wd, xc, hh, cv, c) {
    const st = lay.st;
    if (lay.bm) {                                   // blending composites over the layer outside: each layer is clamped to it so the colour underneath is known
      if (k > 0) { const ph = st.ph[xc]; if (hh > ph) { hh = ph; cv = 0; } }
      c = blendPx(k > 0 ? st.pc[xc] : lay.bgc, c, lay.bm - 1, lay.ba);
    }
    if (lay.arr) for (let x = xs; x < xs + wd; x++) { st.nh[x] = hh; st.nc[x] = c; }
    return [hh, cv, c];
  }
  function emitRun(lay, k, r) {
    const st = lay.st, X = lay.X, cy = lay.cy;
    if (lay.L && r.cv) {                            // antialiasing: the row just outside each edge, the layer colour mixed over what is there by the coverage
      const d = r.h + 1, yt = cy - r.h - 1, yb = cy + r.h;
      const under = (x) => (k > 0 && st.ph[x] >= d ? st.pc[x] : lay.bgc);
      let a = r.x0, cur = under(r.x0);
      for (let x = r.x0 + 1; x <= r.x1; x++) {
        const u = x < r.x1 ? under(x) : -1;
        if (u !== cur) {
          const ec = mix(cur, r.c, tdiv(r.cv * 256, lay.L));
          if (yt >= lay.Y) lay.rect(X + a, yt, x - a, 1, ec);
          if (yb < lay.Y + lay.H) lay.rect(X + a, yb, x - a, 1, ec);
          a = x; cur = u;
        }
      }
    }
    lay.rect(X + r.x0, cy - r.h, r.x1 - r.x0, 2 * r.h, r.c);
  }
  function endLayer(lay) { if (lay.arr) { lay.st.ph.set(lay.st.nh); lay.st.pc.set(lay.st.nc); } }

  /* ---- state ----------------------------------------------------------------------------------------------------------------------- */
  function state() { return { init: false, key: '' }; }
  function reinit(st, ctx, hard) {
    const p = ctx.p, n = p.layers;
    st.hard = hard; st.init = true; st.s = new Array(n).fill(0); st.acc = 0; st.dirty = true;
    st.hist = Array.from({ length: n }, () => new Uint8Array(p.res + 4));
    st.band = Array.from({ length: n }, () => new Array(NB).fill(0));
    st.e = new Array(NB).fill(0); st.eTick = 0; st.ebounds = null;
    st.ph = new Uint8Array(ctx.w); st.nh = new Uint8Array(ctx.w); st.pc = new Uint16Array(ctx.w); st.nc = new Uint16Array(ctx.w);
    st.stride = 1 + tdiv(n * ctx.w, 900); st.cnt = 0;
  }
  function tick(ctx) {
    const p = ctx.p, st = ctx.st, fb = ctx.fb, W = ctx.w, H = ctx.h, X = ctx.x, Y = ctx.y, dt = ctx.dt || 26;
    const cl = colours(ctx);
    // Only the history/band array sizes and the box geometry force a reset; every other change (view, draw, split, colours, ...) just repaints on the next
    // frame and keeps the history, so switching a setting never wipes the picture.
    const hard = [p.layers, p.res, W, H, X, Y].join(','), soft = [p.view, p.draw, p.split, p.outer, p.nest, p.taper, cl.co, cl.ci, cl.bgc, p.aa | 0, p.bmode | 0, p.balpha | 0, p.hstyle | 0].join(',');
    if (!st.init || st.hard !== hard || ctx.force) { reinit(st, ctx, hard); fb.rect(X, Y, W, H, cl.bgc); }   // the solid background: ONE command
    else if (st.soft !== soft) st.dirty = true;
    st.soft = soft;
    if (ctx.paused) return;                // the firmware skips drawing while paused: the picture holds
    const cy = Y + (H >> 1), Hh = (H >> 1) - 1;
    st.cnt = 0;
    const bud = BUDGETS[p.guard | 0];
    if (bud >= 1000000000) st.stride = 1;   // guard OFF: full resolution from the first frame
    const rect = (x, y, w, h, c) => { fb.rect(x, y, w, h, c); st.cnt++; };
    const L = AA_LEVELS[p.aa | 0], bm = p.bmode | 0;
    const lay = { st, rect, X, Y, H, cy, bgc: cl.bgc, L, bm, ba: tdiv((p.balpha === undefined ? 60 : p.balpha) * 255 + 50, 100), arr: !!(L || bm) };
    if (p.view === 1) spectrum(ctx, st, dt, cl, cy, Hh, rect, lay); else history(ctx, st, dt, cl, cy, Hh, rect, lay);
    if (bud < 1000000000 && st.cnt) st.stride = st.cnt > bud ? Math.min(16, st.stride + 1) : (st.cnt < (bud >> 1) && st.stride > 1 ? st.stride - 1 : st.stride);
  }

  /* HISTORY view: x is time (newest left); the box is cleared and every layer drawn outermost first, merging neighbouring cells of equal height
     and colour. `stride` widens the evaluation cell when the last frame needed more than the budget of commands. */
  function history(ctx, st, dt, cl, cy, Hh, rect, lay) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, res = p.res, smooth = p.draw === DRAW.SMOOTH, aaL = lay.L;
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
      const flush = () => { if (run) { emitRun(lay, k, run); run = null; } };
      for (let xs = 0; xs < W; xs += s) {
        const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
        let a8;
        if (smooth) { const t = tdiv((2 * xc + 1) * res * 128, W) - phase - 128; a8 = t <= 0 ? h[0] : cr8((j) => h[j], res + 4, t >> 8, t & 255); }
        else a8 = h[Math.min(res - 1, tdiv(xc * res, W))];
        const w8 = tq === 0 ? 255 : win8(T.winHist, tq, tdiv((2 * xc + 1) * 8192, W)), aaT = aaL ? halfPxAA(a8, w8, Hh, aaL) : null, h0 = aaT ? aaT[0] : halfPx(a8, w8, Hh), cv0 = aaT ? aaT[1] : 0, q = Math.min(AGE - 1, tdiv((2 * xc + 1) * 4, W));
        const [hh, cv, c] = cellOut(lay, k, xs, wd, xc, h0, cv0, cl.tbl[k][q]);
        if (run && run.h === hh && run.c === c && run.cv === cv) { run.x1 = xs + wd; continue; }
        flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh, c, cv };
      }
      flush(); endLayer(lay);
    }
    if (p.taper > 0) for (let j = 0; j < DOTS.length; j++) { const d = DOTS[j]; rect(X + W - 8 - j * 12 - (d >> 1), cy - (d >> 1), d, d, cl.tbl[n - 1][AGE - 1]); }
  }

  /* SPECTRUM view (no scrolling): x is frequency (bass left), the 16 band levels are the outline. With a frequency split every layer draws only the
     part of the axis it listens to (the same ranges as the handles); with DYNAMICS every layer is the whole outline. Layers also differ by response
     time (outer slow, inner fast) and height. Redrawn every frame. Style (experimental): BLOCKS, EQ BELLS (soft shoulders) or CURVE (one outline). */
  function spectrum(ctx, st, dt, cl, cy, Hh, rect, lay) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, smooth = p.draw === DRAW.SMOOTH, tq = tdiv(p.taper + 2, 5), aaL = lay.L, s = Math.max(st.stride, tdiv(W + p.res - 1, p.res));   // Resolution = columns across the width (cell width), never finer than the self-scaling stride allows
    rect(X, ctx.y, W, ctx.h, cl.bgc);
    if (p.split === SPLIT.ENERGY) learnEnergy(st, ctx.spec, n);
    const bnd = p.split === SPLIT.DYNAMICS ? null : boundsFor(p.split, n, st, xoOf(p));   // frequency splits: each layer draws only the part of the axis it listens to
    const hstyle = p.hstyle | 0;
    if (hstyle === 2 && bnd) return curveOutline(ctx, st, dt, cl, cy, Hh, rect, bnd, lay, s);
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
      const flush = () => { if (run) { emitRun(lay, k, run); run = null; } };
      for (let xs = 0; xs < W; xs += s) {
        const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
        let a8;
        if (smooth) { const t = tdiv((2 * xc + 1) * NB * 128, W) - 128; a8 = t <= 0 ? bu[0] : cr8((j) => bu[j], NB, t >> 8, t & 255); }
        else a8 = bu[Math.min(NB - 1, tdiv(xc * NB, W))];
        const w8 = tq === 0 ? 255 : win8(T.winSpec, tq, tdiv((2 * xc + 1) * 8192, W)), aaT = aaL ? halfPxAA(a8, w8, Hh, aaL) : null, h0 = aaT ? aaT[0] : halfPx(a8, w8, Hh), cv0 = aaT ? aaT[1] : 0;
        const [hh, cv, c] = cellOut(lay, k, xs, wd, xc, h0, cv0, cl.base[k]);
        if (run && run.h === hh && run.c === c && run.cv === cv) { run.x1 = xs + wd; continue; }
        flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh, c, cv };
      }
      flush(); endLayer(lay);
    }
  }

  /* CURVE style (experimental): ONE continuous outline across the whole axis, coloured by a gradient whose stops are the layer colours at the centres of
     their frequency ranges, with a smooth gain curve through the per-layer gains (all Q8 integer). Layer 0's response time drives the bands. */
  function curveOutline(ctx, st, dt, cl, cy, Hh, rect, bnd, lay, s) {
    const p = ctx.p, W = ctx.w, X = ctx.x, n = p.layers, smooth = p.draw === DRAW.SMOOTH, tq = tdiv(p.taper + 2, 5), aaL = lay.L;
    const m = n > 1 ? T.mulQ8[n - 1 + 5] : 256, ca = coef(p.response, m, dt, 1), cr = coef(p.response, m, dt, 0), b = st.band[0], bu = new Array(NB);
    const layOf = (g) => (p.outer === 0 ? g : n - 1 - g), cen = [], gdb = [], col = [];
    for (let g = 0; g < n; g++) { cen.push((bnd[g] + bnd[g + 1]) * 128); gdb.push(clamp(p['g' + (layOf(g) + 1)] | 0, 0, 36) - 18); col.push(cl.base[layOf(g)]); }
    const seg = (pos) => {                          // pos in band units, Q8
      if (pos <= cen[0]) return [0, 0, 0];
      if (pos >= cen[n - 1]) return [n - 1, n - 1, 0];
      let g = 0; while (pos > cen[g + 1]) g++;
      return [g, g + 1, tdiv((pos - cen[g]) * 256, cen[g + 1] - cen[g])];
    };
    for (let i = 0; i < NB; i++) {
      const v = tdiv(ctx.spec[i] * 4096, 255); b[i] = stepTo(b[i], v, ca, cr);
      const [g0, g1, f] = seg(i * 256 + 128), dbq = gdb[g0] * 256 + (gdb[g1] - gdb[g0]) * f, vq = dbq + 18 * 256, i0 = vq >> 8, fr = vq & 255;
      const mult = T.gainQ8[i0] + (((T.gainQ8[Math.min(37, i0 + 1)] - T.gainQ8[i0]) * fr) >> 8);
      bu[i] = Math.min(255, (toU8(b[i]) * mult) >> 8);
    }
    let run = null;
    const flush = () => { if (run) { emitRun(lay, 0, run); run = null; } };
    for (let xs = 0; xs < W; xs += s) {
      const wd = Math.min(s, W - xs), xc = Math.min(W - 1, xs + (s >> 1));
      let a8;
      if (smooth) { const t = tdiv((2 * xc + 1) * NB * 128, W) - 128; a8 = t <= 0 ? bu[0] : cr8((j) => bu[j], NB, t >> 8, t & 255); }
      else a8 = bu[Math.min(NB - 1, tdiv(xc * NB, W))];
      const w8 = tq === 0 ? 255 : win8(T.winSpec, tq, tdiv((2 * xc + 1) * 8192, W)), aaT = aaL ? halfPxAA(a8, w8, Hh, aaL) : null, h0 = aaT ? aaT[0] : halfPx(a8, w8, Hh), cv0 = aaT ? aaT[1] : 0;
      const [g0, g1, f] = seg(tdiv((2 * xc + 1) * NB * 128, W));
      const [hh, cv, c] = cellOut(lay, 0, xs, wd, xc, h0, cv0, mix(col[g0], col[g1], f));
      if (run && run.h === hh && run.c === c && run.cv === cv) { run.x1 = xs + wd; continue; }
      flush(); if (hh > 0) run = { x0: xs, x1: xs + wd, h: hh, c, cv };
    }
    flush(); endLayer(lay);
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

  const api = { key: 'layered_wave', state, tick, targets, bandOwner, boundsFor, xoOf, describe, layerColours, ends, EDGES, SPLIT, DRAW, ROLES, BUDGET };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).layered_wave = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
