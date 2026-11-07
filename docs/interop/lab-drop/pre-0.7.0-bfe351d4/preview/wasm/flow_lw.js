#!/usr/bin/env node
/* Which way does Layered Wave flow, and where are its head and tail? The firmware's drawing source as WebAssembly (layered_wave_wasm.js) draws into a small framebuffer that implements the three engine calls it makes (rect,
 * scroll_left, blit); the top edge of the picture is measured. node flow_lw.js prints JSON: for each of HISTORY and SCROLL and both Directions, the dominant horizontal shift of the picture between two pictures (positive =
 * towards larger x, to the right) and the height at 10 % and at 90 % of the width after the history has filled (a steady loud input). Used by sim/test_layered_wave_flow.py (B-965). */
const path = require('path'), fs = require('fs');
const root = path.join(__dirname, '..', '..', '..', '..');
const W = require(path.join(root, 'tools/meters/preview/meters/layered_wave_wasm.js'));
const schema = JSON.parse(fs.readFileSync(path.join(root, 'tools/meters_schema.json'), 'utf8'));
const m = schema.meters.find((x) => x.key === 'layered_wave'), keys = m.params.map((p) => p.key);
const OFFS = ['x', 'y', 'w', 'h', 'bg', 'force', 'paused', 'degrade', 'frame', 'dt_ms', 'peak_l', 'peak_r'];
const WIDTH = { x: 2, y: 2, w: 2, h: 2, bg: 2, force: 1, paused: 1, degrade: 1, dt_ms: 2, frame: 4, peak_l: 4, peak_r: 4 };
const BX = 16, BY = 152, BW = 368, BH = 122, BG = 0x18C3, FW = 400, FH = 360;
function run(mod, over, frames, spec) {
  const px = new Uint16Array(FW * FH);
  const copy = (sx, sy, dx, dy, w, h) => { const rows = []; for (let yy = 0; yy < h; yy++) rows.push(px.slice((sy + yy) * FW + sx, (sy + yy) * FW + sx + w)); for (let yy = 0; yy < h; yy++) px.set(rows[yy], (dy + yy) * FW + dx); };
  const inst = new WebAssembly.Instance(mod, { env: {
    rect: (x, y, w, h, c) => { for (let yy = Math.max(0, y); yy < Math.min(FH, y + h); yy++) px.fill(c, yy * FW + Math.max(0, x), yy * FW + Math.min(FW, x + w)); },
    scroll_left: (x, y, w, h, d) => copy(x + d, y, x, y, w - d, h),
    blit: (sx, sy, dx, dy, w, h) => copy(sx, sy, dx, dy, w, h),
    text: () => 0 } });
  const x = inst.exports, mem = () => x.memory.buffer, dv = () => new DataView(mem());
  const o = new Uint16Array(mem(), x.offs_ptr(), OFFS.length), off = Object.fromEntries(OFFS.map((k, i) => [k, o[i]]));
  const put = (k, v) => { const a = x.in_ptr() + off[k]; if (WIDTH[k] === 1) dv().setUint8(a, v & 255); else if (WIDTH[k] === 2) dv().setUint16(a, v & 65535, true); else dv().setUint32(a, v >>> 0, true); };
  for (let i = 0; i < x.n_roles(); i++) dv().setUint16(x.roles_ptr() + 2 * i, 0x1000 + i * 37, true);
  const P = Object.assign({}, m.presets[0].values, over);
  keys.forEach((k, i) => dv().setUint16(x.params_ptr() + 2 * i, P[k] | 0, true));
  let seed = 7; const prof = [];
  const profile = () => { const bg = px[BY * FW + BX + 1], row = []; for (let c = 0; c < BW; c++) { let t = 0; for (let yy = BY; yy < BY + BH; yy++) if (px[yy * FW + BX + c] !== bg) { t = BH - 2 * (yy - BY); break; } row.push(Math.max(0, t)); } return row; };
  for (let n = 0; n < frames; n++) {
    put('x', BX); put('y', BY); put('w', BW); put('h', BH); put('bg', BG); put('force', n === 0 ? 1 : 0); put('paused', 0); put('dt_ms', 26); put('frame', n);
    const sp = new Uint8Array(mem(), x.spec_ptr(), 16); for (let i = 0; i < 16; i++) sp[i] = spec(n, i, () => { seed = (seed * 1664525 + 1013904223) >>> 0; return (seed >>> 16) & 255; });
    x.tick_raw();
    if (n >= frames - 30) prof.push(profile());
  }
  return prof;
}
function shift(prof0) {                                 // the dominant shift of frame n+1 against frame n, after taking away the fixed taper window (the mean of each column)
  const mean = prof0[0].map((_, c) => prof0.reduce((a, r) => a + r[c], 0) / prof0.length), prof = prof0.map((r) => r.map((v, c) => v - mean[c]));
  const hist = {};
  for (let n = 0; n + 1 < prof.length; n++) { let best = 0, bs = 1e18; for (let s = -12; s <= 12; s++) { let e = 0; for (let c = 60; c < 300; c++) e += Math.abs(prof[n][c] - (prof[n + 1][c + s] || 0)); if (e < bs) { bs = e; best = s; } } hist[best] = (hist[best] || 0) + 1; }
  return Object.entries(hist).sort((a, b) => b[1] - a[1])[0][0] | 0;
}
WebAssembly.compile(new Uint8Array(Buffer.from(W.b64, 'base64'))).then((mod) => {
  const out = {};
  const loud = (n, i, r) => ((n >> 4) & 1) ? 230 - (r() & 15) : 25 + (r() & 15);          // bursts of loud and quiet every 16 pictures: the edge has features to follow
  for (const [name, view] of [['history', 0], ['scroll', 2]]) for (const dir of [0, 1]) {
    const over = { view, direction: dir, draw: 1, taper: 70, taper_head: 40, taper_tail: 30, speed: 120, res: 368, response: 8 };
    const prof = run(mod, over, 360, loud);
    const flat = run(mod, over, 360, () => 200);      // a steady loud input: the envelope shows the window alone
    const h = (p, f) => p[p.length - 1][Math.round(BW * f)];
    out[name + dir] = { shift: shift(prof), h10: h(flat, 0.10), h90: h(flat, 0.90) };
  }
  console.log(JSON.stringify(out));
});
