#!/usr/bin/env node
/* Emits golden scenarios for sim/test_layered_wave_golden.py: for the presets and a grid of parameter variations, the exact input trace and the
 * command log the JS integer twin produced. The Python test feeds the same trace to the REAL fw/layered_wave.inc compiled on the host and
 * compares the logs line for line. */
const path = require('path'), cp = require('child_process');
const root = path.join(__dirname, '..', '..', '..');
const Run = require('./tau_run.js'), Audio = require('./tau_audio.js');
const data = JSON.parse(cp.execFileSync('python3', ['-c', 'import sys,json;sys.path.insert(0,"tools/meters/preview");import build;print(json.dumps(build.data()))'], { cwd: root, maxBuffer: 1 << 26 }).toString());
const m = data.schema.meters.find((x) => x.key === 'layered_wave');
const keys = m.params.map((p) => p.key), base = m.presets[0].values;
const ROLES = ['accent', 'text_primary', 'text_secondary', 'ok', 'warn', 'danger', 'pill', 'error', 'surface', 'surface_track', 'base', 'bg_bottom'];
const scen = [], FRAMES = 70;
const boxes = { normal: { x: 16, y: 152, w: 368, h: 122 }, full: { x: 0, y: 0, w: 400, h: 323 } };
function add(name, vals, box, seed, light, accent) {
  const params = Object.assign({}, base, vals);
  const demo = Audio.demo(seed);
  const source = (n) => { const f = demo(n); return { spec: f.spec, wave: f.wave, paused: n >= 40 && n < 46, post: true }; };   // a short pause in the middle: the picture must hold
  const r = Run.run({ data, schema: data.schema, key: 'layered_wave', params, source, frames: FRAMES, themeIdx: 0, light, accentIdx: accent, log: true, box: boxes[box] });
  const frames = []; for (let n = 0; n < FRAMES; n++) { const f = source(n); frames.push({ paused: f.paused ? 1 : 0, spec: f.spec, wave: f.wave }); }
  const roles = ROLES.map((k) => (k === 'accent' ? r.theme.accent : r.theme.role[k]));
  scen.push({ name, params: keys.map((k) => params[k]), params2: keys.map((k) => params[k]), chg: -1, box: boxes[box], roles, frames, log: r.logs });
}
m.presets.forEach((pr, i) => add('preset/' + pr.name, pr.values, i % 3 === 2 ? 'full' : 'normal', 3 + i, i % 2 === 1, 1 + (i * 2) % 17));
let seed = 40;
for (const view of [0, 1]) for (const draw of [0, 1]) for (const layers of [1, 3, 6]) for (const split of [0, 1, 2, 3]) {
  const nest = (layers + split) % 2, outer = (layers + draw) % 2;
  add(`grid/v${view}d${draw}n${layers}s${split}`, { view, draw, layers, split, nest, outer, res: draw ? 400 : 120, taper: (layers * 15) % 100, speed: 60 + layers * 20, response: 20 + split * 20 }, 'normal', seed++, 0, 13);
}
for (const [cm, grad] of [[0, 0], [0, 1], [0, 2], [0, 3], [2, 0]]) add(`colour/m${cm}g${grad}`, { color_mode: cm, grad, custom_outer: 0xF81F, custom_inner: 0x07E0, custom_bg: 0x1082, layers: 4 }, 'normal', seed++, grad & 1, 13);
for (const [layers, xo] of [[2, [4]], [3, [2, 9]], [4, [7, 3, 12]], [6, [1, 2, 3, 14, 15]], [5, [15, 15, 2, 2]]]) {   // CUSTOM split: ordinary, unsorted and colliding boundaries
  const v = { split: 4, layers, nest: layers & 1, view: 0, draw: layers & 1 };
  xo.forEach((b, i) => { v['xo' + (i + 1)] = b; });
  add(`custom/n${layers}`, v, 'normal', seed++, 0, 13);
  add(`custom-spectrum/n${layers}`, Object.assign({}, v, { view: 1, draw: 1 - (layers & 1) }), 'normal', seed++, 0, 13);
}
// The experimental group (Diagnostic Build only): each setting alone and combined, in both views and with a cost guard that engages
for (const [name, v] of [['hstyle1', { hstyle: 1, view: 1, draw: 1, split: 0 }], ['hstyle2', { hstyle: 2, view: 1, draw: 0, split: 1, g1: 24, g2: 12, g3: 18, g4: 30 }], ['aa2', { aa: 2 }], ['aa3', { aa: 3, layers: 5, view: 1 }],
  ['bm1', { bmode: 1 }], ['bm2', { bmode: 2, balpha: 40 }], ['bm3', { bmode: 3, layers: 5 }], ['bm4', { bmode: 4, view: 1, draw: 1 }], ['bm5', { bmode: 5, balpha: 30, layers: 4 }],
  ['aa+bm', { aa: 1, bmode: 1, res: 400, view: 1 }], ['guard1', { guard: 1, res: 400, layers: 6 }], ['guard2', { guard: 2, res: 400, aa: 2, layers: 6 }],
  ['gains', { g1: 0, g2: 36, g3: 6, g4: 28, g5: 18, g6: 10, layers: 6, view: 1, split: 0 }]]) { add('exp/' + name, v, 'normal', seed++, 0, 5); add('exp-full/' + name, v, 'full', seed++, 1, 9); }
// A setting changed in the middle of a run: only sizes and geometry reset, everything else must keep the history and just repaint (firmware and lab alike).
const Th = require('./tau_theme.js'), { Fb } = require('./tau_fb.js'), LW = require('./meters/layered_wave.js');
function addChange(name, v1, v2, chg) {
  const theme = Th.makeTheme(data, 0, false, 13, 360), fb = new Fb(400, 360, true), st = LW.state(), box = boxes.normal, demo = Audio.demo(77);
  const p1 = Object.assign({}, base, v1), p2 = Object.assign({}, p1, v2), logs = [], frames = [];
  for (let n = 0; n < FRAMES; n++) {
    const f = demo(n), paused = n >= 40 && n < 46, l0 = fb.log.length;
    LW.tick({ fb, x: box.x, y: box.y, w: box.w, h: box.h, theme, spec: f.spec, wave: f.wave, paused, p: n >= chg ? p2 : p1, st, force: n === 0 });
    logs.push(fb.log.slice(l0)); frames.push({ paused: paused ? 1 : 0, spec: f.spec, wave: f.wave });
  }
  scen.push({ name, params: keys.map((k) => p1[k]), params2: keys.map((k) => p2[k]), chg, box, roles: ROLES.map((k) => (k === 'accent' ? theme.accent : theme.role[k])), frames, log: logs });
}
addChange('change/view-0-to-1', { view: 0, draw: 0, layers: 3 }, { view: 1 }, 30);
addChange('change/view-1-to-0', { view: 1, draw: 1, layers: 4 }, { view: 0 }, 30);
addChange('change/colour', { draw: 0, res: 64 }, { color_mode: 2, custom_outer: 0xF81F, custom_inner: 0x07E0, custom_bg: 0x1082 }, 25);
addChange('change/split-and-taper', { draw: 0, res: 48, layers: 4 }, { split: 4, xo1: 3, xo2: 7, xo3: 12, taper: 20 }, 35);
addChange('change/layers-hard', { draw: 1, layers: 3 }, { layers: 5 }, 30);
add('stress/res400-6layers-full', { layers: 6, res: 400, draw: 1, speed: 240 }, 'full', 99, 0, 5);
process.stdout.write(JSON.stringify(scen));
