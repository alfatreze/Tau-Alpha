#!/usr/bin/env node
/* Emits golden scenarios for sim/test_waterfall_golden.py: for the factory presets and a grid of parameter variations, the exact input trace (per frame: spectrum, pause, degrade, force, box) and the engine command log
 * the lab's WebAssembly twin of the Waterfall Grid (meters/waterfall_grid.js, the pack's own source compiled by wasm/build_wasm.py) produced. The Python test feeds the same trace to the REAL fw2/ui/meter_waterfall_grid.c
 * compiled natively with printing stubs for the engine calls and compares the logs line for line. */
const path = require('path'), cp = require('child_process');
const root = path.join(__dirname, '..', '..', '..');
const Audio = require('./tau_audio.js'), Th = require('./tau_theme.js'), { Fb } = require('./tau_fb.js'), WF = require('./meters/waterfall_grid.js');
const data = JSON.parse(cp.execFileSync('python3', ['-c', 'import sys,json;sys.path.insert(0,"tools/meters/preview");import build;print(json.dumps(build.data()))'], { cwd: root, maxBuffer: 1 << 26 }).toString());
const m = data.schema.meters.find((x) => x.key === 'waterfall_grid'), keys = m.params.map((p) => p.key);
const base = Object.assign({}, ...m.params.map((p) => ({ [p.key]: p.default })));
const ROLES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar', 'fs_red', 'fs_track'];
const boxes = { normal: { x: 16, y: 152, w: 368, h: 122 }, full: { x: 0, y: 0, w: 400, h: 323 }, small: { x: 40, y: 200, w: 200, h: 80 } };
const FRAMES = 160, scen = [];
function add(name, vals, boxName, seed, light, accent, box2At, box2Name) {
  const params = Object.assign({}, base, vals), theme = Th.makeTheme(data, 0, !!light, accent, 360), fb = new Fb(400, 360, true), st = WF.state(), demo = Audio.demo(seed);
  const frames = [], logs = [];
  for (let n = 0; n < FRAMES; n++) {
    const f = demo(n), b = box2At >= 0 && n >= box2At ? boxes[box2Name] : boxes[boxName];
    const fr = { paused: n >= 40 && n < 52 ? 1 : 0, degrade: n >= 60 && n < 100 ? 1 : n >= 100 && n < 130 ? 2 : 0, force: n === 0 || n === 30 ? 1 : 0, box: b, spec: f.spec.map((v) => Math.max(0, Math.min(255, Math.trunc(v)))) };
    const l0 = fb.log.length;
    WF.tick({ fb, x: b.x, y: b.y, w: b.w, h: b.h, bg: 0x18C3, theme, spec: fr.spec, paused: !!fr.paused, p: params, st, force: !!fr.force, degrade: fr.degrade, dt_ms: 26 });
    logs.push(fb.log.slice(l0)); frames.push(fr);
  }
  scen.push({ name, params: keys.map((k) => params[k]), roles: ROLES.map((k) => (k === 'accent' ? theme.accent : theme.role[k] || 0)), frames, log: logs });
}
m.presets.forEach((pr, i) => add('preset/' + pr.name, pr.values, i % 3 === 2 ? 'full' : 'normal', 3 + i, i & 1, 1 + (i * 2) % 17));
let seed = 40;
for (const mode of [0, 1, 2, 3]) for (const flow of [0, 1]) for (const palette of [0, 1, 3]) for (const cell of [5, 8, 12])
  add(`grid/m${mode}f${flow}p${palette}c${cell}`, { mode, flow, palette, cell, speed: 60 + cell * 10, gain: 100 + palette * 30, fade: 300 }, cell === 12 ? 'small' : 'normal', seed++, palette & 1, 13);
add('event/box-change-spectrogram-v', { mode: 0, flow: 0, palette: 1 }, 'normal', seed++, 0, 13, 70, 'full');
add('event/box-change-spectrogram-h', { mode: 0, flow: 1, palette: 0 }, 'normal', seed++, 0, 13, 70, 'small');
add('event/box-change-sparkle', { mode: 3, cell: 6 }, 'normal', seed++, 0, 13, 70, 'full');
add('stress/fullscreen-smallest-cell', { mode: 0, cell: 5, speed: 240, palette: 1 }, 'full', seed++, 0, 5);
process.stdout.write(JSON.stringify(scen));
