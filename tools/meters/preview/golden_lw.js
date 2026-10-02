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
  scen.push({ name, params: keys.map((k) => params[k]), box: boxes[box], roles, frames, log: r.logs });
}
m.presets.forEach((pr, i) => add('preset/' + pr.name, pr.values, i % 3 === 2 ? 'full' : 'normal', 3 + i, i % 2 === 1, 1 + i * 2));
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
}
add('stress/res400-6layers-full', { layers: 6, res: 400, draw: 1, speed: 240 }, 'full', 99, 0, 5);
process.stdout.write(JSON.stringify(scen));
