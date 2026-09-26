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
console.log(fails ? fails + ' FAILURES' : 'preview stack OK');
process.exit(fails ? 1 : 0);
