#!/usr/bin/env node
/* Emits golden scenarios for sim/test_meter_wasm_golden.py: for Winamp Bars, Winamp Scope, Master VU and Chladni (owner decision D-M15: every Player 2 meter is a WebAssembly module of its firmware source), the factory
 * presets and, per parameter, its lowest and highest value, in the normal and the fullscreen box, with a pause, the degrade levels, a forced repaint and a moved box. Per scenario: the exact input trace (per frame:
 * spectrum, peaks, the Scope's capture words, pause, degrade, force, box) and the engine command log the lab's module (meters/<key>.js, through the adapter) produced. The Python test feeds the same trace to the REAL
 * fw2/ui/meter_<key>.c compiled natively with printing stubs for the engine calls and the hardware, and compares the logs line for line. */
const path = require('path'), cp = require('child_process');
const root = path.join(__dirname, '..', '..', '..');
const Audio = require('./tau_audio.js'), Th = require('./tau_theme.js'), { Fb } = require('./tau_fb.js');
const data = JSON.parse(cp.execFileSync('python3', ['-c', 'import sys,json;sys.path.insert(0,"tools/meters/preview");import build;print(json.dumps(build.data()))'], { cwd: root, maxBuffer: 1 << 26 }).toString());
const fs = require('fs');
const EXPLICIT = ['winamp_bars', 'winamp_scope', 'vu_master', 'chladni'], OTHER_TESTS = new Set(['layered_wave', 'sound_map', 'waterfall_grid']);        // the others have their own golden tests
const GENERIC = fs.readdirSync(path.join(root, 'meters')).map((d) => { try { return JSON.parse(fs.readFileSync(path.join(root, 'meters', d, 'meter.json'), 'utf8')); } catch (e) { return null; } })
  .filter((m) => m && m.p2 && !EXPLICIT.includes(m.key) && !OTHER_TESTS.has(m.key) && fs.existsSync(path.join(__dirname, 'meters', m.key + '_wasm.js'))).map((m) => m.key);       // a meter made by new_meter.py (scaffolded source)
const KEYS = EXPLICIT.concat(GENERIC);
const ROLES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar', 'fs_red', 'fs_track'];
const boxes = { normal: { x: 16, y: 152, w: 368, h: 122 }, full: { x: 0, y: 0, w: 400, h: 323 } };
const FRAMES = 150, scen = [];
function add(key, name, vals, boxName, seed, light, accent, blend, box2At, box2Name) {
  const M = require('./meters/' + key + '.js'), m = data.schema.meters.find((x) => x.key === key), keys = m.params.map((p) => p.key);
  const params = Object.assign({}, ...m.params.map((p) => ({ [p.key]: p.default })), vals), theme = Th.makeTheme(data, 0, !!light, accent, 360), fb = new Fb(400, 360, true), st = M.state(), demo = Audio.demo(seed);
  fb.blendReady = !!blend;
  const frames = [], logs = [];
  for (let n = 0; n < FRAMES; n++) {
    const f = demo(n), b = box2At >= 0 && n >= box2At ? boxes[box2Name] : boxes[boxName], spec = f.spec.map((v) => Math.max(0, Math.min(255, Math.trunc(v))));
    const mean = spec.reduce((a, v) => a + v, 0) / 16, pl = Math.trunc(Math.min(24575, mean * 24575 / 255 * 1.4)), pr = Math.trunc(Math.min(24575, pl * (0.7 + 0.3 * Math.sin(n / 7))));
    const cap = key === 'winamp_scope' ? M.capture(f.wave).map((c) => (((c[0] & 0xFFFF) << 16) | (c[1] & 0xFFFF)) >>> 0) : new Array(256).fill(0);
    const fr = { paused: n >= 40 && n < 52 ? 1 : 0, degrade: n >= 60 && n < 100 ? 1 : n >= 100 && n < 130 ? 2 : 0, force: n === 0 || n === 30 ? 1 : 0, box: b, spec, pl, pr, cap };
    const l0 = fb.log.length;
    M.tick({ fb, x: b.x, y: b.y, w: b.w, h: b.h, bg: 0x18C3, theme, spec, wave: f.wave, capture: key === 'winamp_scope' ? M.capture(f.wave) : undefined, vu: key === 'vu_master' ? { l: pl, r: pr } : undefined, paused: !!fr.paused, p: params, st, force: !!fr.force, degrade: fr.degrade, dt_ms: 26 });
    logs.push(fb.log.slice(l0)); frames.push(fr);
  }
  scen.push({ key, name: key + '/' + name, blend: blend ? 1 : 0, params: keys.map((k) => params[k]), roles: ROLES.map((k) => (k === 'accent' ? theme.accent : theme.role[k] || 0)), frames, log: logs });
}
let seed = 3;
for (const key of KEYS) {
  const m = data.schema.meters.find((x) => x.key === key);
  m.presets.forEach((pr, i) => add(key, 'preset/' + pr.name, pr.values, i % 3 === 2 ? 'full' : 'normal', seed++, i & 1, 1 + (i * 2) % 17, i % 4 !== 3));
  for (const p of m.params) {
    const lo = p.type === 'enum' || p.type === 'bool' ? 0 : p.min, hi = p.type === 'enum' ? p.values.length - 1 : p.type === 'bool' ? 1 : p.max;
    for (const v of lo === hi ? [lo] : [lo, hi]) add(key, `param/${p.key}=${v}`, { [p.key]: v }, 'normal', seed++, 0, 13, true);
  }
  add(key, 'event/box-change', {}, 'normal', seed++, 0, 13, true, 70, 'full');
  add(key, 'event/fullscreen', {}, 'full', seed++, 1, 5, false);
}
process.stdout.write(JSON.stringify(scen));
