#!/usr/bin/env node
/* Node tests for the preview stack: the JS twins equal the C core's vectors and the Python theme mirrors; runs are deterministic and
 * stay inside the meter's declared cost class. The C-vs-JS golden frame comparison is sim/test_meter_golden.py. */
const fs = require('fs'), path = require('path'), assert = require('assert');
const here = __dirname, root = path.join(here, '..', '..', '..');
const C = require('./tau_core.js'), Th = require('./tau_theme.js'), Run = require('./tau_run.js'), Audio = require('./tau_audio.js');
let fails = 0;
const check = (name, fn) => { try { fn(); console.log('ok   ' + name); } catch (e) { fails++; console.log('FAIL ' + name + ': ' + e.message); } };

const cv = JSON.parse(fs.readFileSync(path.join(here, 'fixtures', 'core_vectors.json')));
check('core twin: ease vectors', () => { for (const v of cv.ease) { const vel = { v: v.vel_in }; const o = C.ease(v.cur, v.target, v.mode, v.rate, vel); assert.strictEqual(o, v.out); assert.strictEqual(vel.v, v.vel_out); } });
check('core twin: peak-cap vectors', () => { for (const run of cv.peak) { const p = C.newPeak(); for (const s of run.steps) { C.peakStep(p, s.disp, run.cfg, 26); assert.deepStrictEqual([p.peak, p.vel, p.hold], [s.peak, s.vel, s.hold]); } } });
check('core twin: band-mapping vectors', () => { for (const v of cv.band) assert.deepStrictEqual(Array.from({ length: v.bands }, (_, b) => C.bandTarget(v.spec, 16, v.bands, b)), v.out); });

const tv = JSON.parse(fs.readFileSync(path.join(here, 'fixtures', 'theme_vectors.json')));
const build = require('child_process').execFileSync('python3', [path.join(here, 'build.py'), '--out', '/dev/null'], { cwd: root });   // data() is in python; read it via a tiny dump
const data = JSON.parse(require('child_process').execFileSync('python3', ['-c', 'import sys,json;sys.path.insert(0,"tools/meters/preview");import build;print(json.dumps(build.data()))'], { cwd: root, maxBuffer: 1 << 26 }).toString());
check('theme twin: effective accent for every palette colour', () => { for (const a of tv.accent) { assert.strictEqual(Th.accentOf(a.c565, false, tv.lightAccMaxL), a.dark); assert.strictEqual(Th.accentOf(a.c565, true, tv.lightAccMaxL), a.light); } });
check('theme twin: background ramp rows, both polarities, both directions', () => {
  for (const g of tv.grad) { const gr = Th.makeGradAt(g.top, g.bottom, 360); for (const [y, v] of Object.entries(g.rows)) assert.strictEqual(gr(+y), v, `theme ${g.theme} light ${g.light} accent ${g.accent} row ${y}`); }
});
check('theme twin: gradTop equals the Python mirror', () => {
  for (const g of tv.grad) { const t = data.themes[g.theme], d = g.light ? t.light : t.dark; const acc = Th.accentOf(data.palette[g.accent].c565, g.light, data.lightAccMaxL); assert.strictEqual(Th.gradTop(acc, d.bg_luma), g.top); }
});

const schema = data.schema;
for (const key of ['winamp_bars', 'winamp_scope']) {
  const opts = { data, schema, key, source: Audio.demo(1), frames: 120, log: false };
  check(`${key}: deterministic (same trace, same frame)`, () => { const a = Run.run(opts).fb.checksum(), b = Run.run(opts).fb.checksum(); assert.strictEqual(a, b); });
  check(`${key}: within its declared cost class`, () => { const r = Run.run(opts); assert.ok(!r.cost.over, `worst ${r.cost.max} > budget ${r.cost.budget}`); });
  check(`${key}: paints something and stays inside its box`, () => {
    const r = Run.run(opts), fb = r.fb, bx = { x: 16, y: 152, w: 368, h: 122 };
    const bg = Run.run(Object.assign({}, opts, { frames: 0 })).fb;               // frame 0 = untouched gradient
    let inside = 0, outside = 0;
    for (let y = 0; y < 360; y++) for (let x = 0; x < 400; x++) if (fb.px[y * 400 + x] !== bg.px[y * 400 + x]) { (x >= bx.x && x < bx.x + bx.w && y >= bx.y && y < bx.y + bx.h) ? inside++ : outside++; }
    assert.ok(inside > 500, 'nothing drawn'); assert.strictEqual(outside, 0, outside + ' pixels outside the meter box');
  });
}
check('winamp_bars: Light theme renders too (accent capped, box respected)', () => { const r = Run.run({ data, schema, key: 'winamp_bars', source: Audio.demo(1), frames: 30, light: true }); assert.ok(r.theme.accent !== undefined && r.cost.max > 0); });

/* layered_wave (firmware: fw/layered_wave.inc; the C-vs-JS golden frames are sim/test_layered_wave_golden.py) */
{
  const LW = require('./meters/layered_wave.js'), lm = schema.meters.find((m) => m.key === 'layered_wave');
  const box = { x: 16, y: 152, w: 368, h: 122 };
  check('layered_wave: registered, selectable, grouped parameters within MTR_MAX_PARAMS', () => { assert.ok(lm && lm.selectable && !lm.planned && lm.caps.every((c) => ['rect', 'hw_spectrum', 'hw_wave'].includes(c))); assert.ok(lm.params.length <= 40 && lm.groups.length === 5 && lm.params.every((x) => lm.groups.some((g) => g.key === x.group)));
    for (const x of lm.params) assert.ok(x.help, x.key + ' has no (i) help text'); assert.ok(lm.presets.length <= 16 && lm.presets.some((q) => q.experimental) && lm.presets.every((q) => !!q.experimental === /\[EX\]$/.test(q.name)) && !lm.presets[lm.default_preset].experimental); });
  lm.presets.forEach((pr, pi) => {
    const opts = { data, schema, key: 'layered_wave', params: pr.values, source: Audio.demo(2), frames: 200 };
    check(`layered_wave/${pr.name}: deterministic, inside its box, within the cost class`, () => {
      const a = Run.run(opts), b = Run.run(opts); assert.strictEqual(a.fb.checksum(), b.fb.checksum()); assert.ok(pr.experimental || !a.cost.over, `worst ${a.cost.max} > ${a.cost.budget}`);   // [EX] presets trade cost for quality on purpose
      const bg = Run.run(Object.assign({}, opts, { frames: 0 })).fb; let inside = 0, outside = 0;
      for (let y = 0; y < 360; y++) for (let x = 0; x < 400; x++) if (a.fb.px[y * 400 + x] !== bg.px[y * 400 + x]) ((x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) ? inside++ : outside++);
      assert.ok(inside > 300, 'nothing drawn'); assert.strictEqual(outside, 0, outside + ' pixels outside the box');
    });
  });
  check('layered_wave: SPECTRUM view draws inside the box in every draw mode, and does not scroll', () => {
    for (const draw of [0, 1, 2]) {
      const base = Object.assign({}, lm.presets[0].values, { view: 1, draw }), run = (f) => Run.run({ data, schema, key: 'layered_wave', params: base, source: () => ({ spec: new Array(16).fill(160), wave: new Array(64).fill(0), paused: false }), frames: f });
      const a = run(120), b = run(121); assert.strictEqual(a.fb.checksum(), b.fb.checksum(), 'a steady input must give a steady picture'); assert.ok(a.cost.mean > 0 && !a.cost.over, 'draw ' + draw + ' cost ' + a.cost.max);
    }
  });
  check('layered_wave: every colour source and gradation renders; custom colours are stored as the exact RGB565 given', () => {
    const run = (v) => Run.run({ data, schema, key: 'layered_wave', params: Object.assign({}, lm.presets[0].values, v), source: Audio.demo(2), frames: 60, accentIdx: 13 });
    const sums = new Set();
    for (const g of [0, 1, 2, 3]) { const r = run({ color_mode: 0, grad: g }); assert.ok(r.cost.mean > 0); sums.add(r.fb.checksum()); }
    assert.strictEqual(sums.size, 4, 'the four gradations must look different for a coloured accent');
    const r = run({ taper: 0, color_mode: 2, custom_outer: 0xF81F, custom_inner: 0x07E0, custom_bg: 0x0000 }), ends = LW.ends({ p: r.params, theme: r.theme });
    assert.deepStrictEqual(ends, [0xF81F, 0x07E0, 0]);
    const px = new Set(Array.from(r.fb.px)); assert.ok(px.has(0xF81F) || px.has(0x07E0), 'custom colours appear on screen');
  });
  check('layered_wave: fullscreen box (400x323) renders inside its box', () => {
    const r = Run.run({ data, schema, key: 'layered_wave', params: lm.presets[0].values, source: Audio.demo(2), frames: 120, box: { x: 0, y: 0, w: 400, h: 323 } });
    assert.ok(r.cost.mean > 0);
    for (let y = 323; y < 360; y++) for (let x = 0; x < 400; x++) assert.strictEqual(r.fb.px[y * 400 + x], Run.run({ data, schema, key: 'layered_wave', params: lm.presets[0].values, source: Audio.demo(2), frames: 0, box: { x: 0, y: 0, w: 400, h: 323 } }).fb.px[y * 400 + x]);
  });
  check('layered_wave: nested layers never cross, for every split and both orders; groups tile the 16 bands', () => {
    const rng = Audio.rng(7);
    for (const split of [0, 1, 2, 4]) for (const outer of [0, 1]) for (let n = 1; n <= 6; n++) {
      const p = { layers: n, split, outer, nest: 0, xo1: 4, xo2: 2, xo3: 9, xo4: 9, xo5: 15 }, st = { e: new Array(16).fill(6500), eTick: 0, ebounds: null };
      for (let t = 0; t < 40; t++) {
        const spec = Array.from({ length: 16 }, () => Math.round(rng() * 255)), tg = LW.targets(p, spec, [], st);
        for (let k = 1; k < n; k++) assert.ok(tg[k] <= tg[k - 1] + 1e-9, `split ${split} outer ${outer} n ${n}: layer ${k} above layer ${k - 1}`);
      }
      const b = LW.boundsFor(split, n, st, LW.xoOf(p)); assert.strictEqual(b[0], 0); assert.strictEqual(b[n], 16); for (let i = 1; i <= n; i++) assert.ok(b[i] > b[i - 1]);
    }
  });
}

console.log(fails ? fails + ' FAILURES' : 'preview stack OK');
process.exit(fails ? 1 : 0);
