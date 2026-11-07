/* layered_wave: nested, mirrored envelope layers on a SOLID background (docs/features/meters/LAYERED_WAVE_METER_SPEC.md).
 * ctx = {fb, x, y, w, h, bg, theme, spec[16] (0..255, after the firmware's spectrum ballistics), wave[64] (-100..100), paused, p (parameters by key), st, force, dt}.
 *
 * Since B-975 this is NOT a port: the drawing is the firmware's own source (fw2/ui/layered_wave.inc through fw2/ui/meter_layered_wave.c) compiled to WebAssembly
 * (wasm/layered_wave_wasm.c, tools/meters/preview/wasm/build_wasm.py; committed as layered_wave_wasm.js), so the lab, the golden test and Tau Omega draw what the Pocket
 * draws, command for command. The engine calls land in the lab's framebuffer (rect, blit, scrollLeft). What stays here is the lab's own help, not drawing: the colours
 * of the split map, the band split and the layer targets (checked by test.js), and the bridge to the module. The module compiles asynchronously in a browser (ready());
 * in node the compile is synchronous, so Run.run and the tests need no waiting. Differences from the Pocket, on purpose: the cost guard counts commands (a browser cannot
 * time the CPU), and the ENERGY split's learned boundaries live inside the module (the split map shows the default boundaries for it).
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
      const row = []; for (let q = 0; q < AGE; q++) row.push(base[k]);          /* the Taper shapes the tail only: no colour fade towards the background (owner, DEV 210); kept equal to fw2/ui/layered_wave.inc lw_colours */
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

  /* ---- the module ---------------------------------------------------------------------------------------------------------------------------- */
  const W = () => (typeof module !== 'undefined' ? require('./layered_wave_wasm.js') : root.TauLayeredWaveWasm);     // looked up when first used: in the lab page the data file comes after this one
  const MROLES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar', 'fs_red', 'fs_track'];     // the TR_* order of fw2/ui/theme.h
  const OFFS = ['x', 'y', 'w', 'h', 'bg', 'force', 'paused', 'degrade', 'frame', 'dt_ms', 'peak_l', 'peak_r'];
  const WIDTH = { x: 2, y: 2, w: 2, h: 2, bg: 2, force: 1, paused: 1, degrade: 1, dt_ms: 2, frame: 4, peak_l: 4, peak_r: 4 };
  let compiled = null, compiling = null;
  const bytes = () => (typeof module !== 'undefined' ? new Uint8Array(Buffer.from(W().b64, 'base64')) : Uint8Array.from(atob(W().b64), (c) => c.charCodeAt(0)));
  function ready() { if (!compiling) compiling = WebAssembly.compile(bytes()).then((m) => { compiled = m; return true; }); return compiling; }
  function state() { return { inst: null, cur: null, frames: 0 }; }
  function instantiate(st) {
    if (!compiled) { if (typeof module === 'undefined') return false; compiled = new WebAssembly.Module(bytes()); }       // node: a synchronous compile is fine
    st.inst = new WebAssembly.Instance(compiled, { env: {
      rect: (x, y, w, h, c) => st.cur.fb.rect(x, y, w, h, c),
      scroll_left: (x, y, w, h, d) => st.cur.fb.scrollLeft(x, y, w, h, d),
      blit: (sx, sy, dx, dy, w, h) => st.cur.fb.blit(sx, sy, dx, dy, w, h),
      text: () => 0 } });
    const x = st.inst.exports, o = new Uint16Array(x.memory.buffer, x.offs_ptr(), OFFS.length);
    st.off = Object.fromEntries(OFFS.map((k, i) => [k, o[i]]));
    return true;
  }
  function put(st, field, v) {
    const dv = new DataView(st.inst.exports.memory.buffer), a = st.inst.exports.in_ptr() + st.off[field];
    if (WIDTH[field] === 1) dv.setUint8(a, v & 255); else if (WIDTH[field] === 2) dv.setUint16(a, v & 65535, true); else dv.setUint32(a, v >>> 0, true);
  }
  function tick(ctx) {
    const st = ctx.st;
    if (!st.inst && !instantiate(st)) { ready(); return; }          // a browser draws nothing until the module is compiled
    st.cur = ctx;
    const x = st.inst.exports, mem = x.memory.buffer, dv = new DataView(mem), keys = W().params;
    if (keys.length !== x.n_params()) throw new Error('layered_wave: the module has ' + x.n_params() + ' parameters, its key list ' + keys.length);
    const th = ctx.theme;
    MROLES.forEach((k, i) => dv.setUint16(x.roles_ptr() + 2 * i, k === 'accent' ? th.accent : (th.role[k] || 0), true));
    keys.forEach((k, i) => dv.setUint16(x.params_ptr() + 2 * i, ctx.p[k] == null ? 0 : ctx.p[k] & 65535, true));
    new Uint8Array(mem, x.spec_ptr(), 16).set(Array.from(ctx.spec, (v) => Math.max(0, Math.min(255, Math.trunc(v)))));
    if (ctx.wave) new Int8Array(mem, x.wave_ptr(), Math.min(ctx.wave.length, x.n_wave())).set(Array.from(ctx.wave, (v) => Math.max(-128, Math.min(127, Math.trunc(v)))).slice(0, x.n_wave()));
    const vals = { x: ctx.x, y: ctx.y, w: ctx.w, h: ctx.h, bg: ctx.bg || 0, force: ctx.force ? 1 : 0, paused: ctx.paused ? 1 : 0, degrade: ctx.degrade || 0, frame: st.frames++, dt_ms: ctx.dt_ms || ctx.dt || 26, peak_l: 0, peak_r: 0 };
    for (const k of OFFS) put(st, k, vals[k]);
    x.tick_raw();                                                    // the waveform as the lab's source gives it (the golden test feeds the same)
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

  const api = { key: 'layered_wave', ready, state, tick, targets, bandOwner, boundsFor, xoOf, describe, layerColours, ends, EDGES, SPLIT, DRAW, ROLES, BUDGET };
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).layered_wave = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
