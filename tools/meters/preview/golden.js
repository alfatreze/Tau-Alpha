#!/usr/bin/env node
/* Emits golden scenarios for sim/test_meter_golden.py: for each Winamp preset, the exact input trace (post-ballistics spec levels,
 * waveform, paused flag) and the command log the JS module produced. The Python test feeds the same trace to the REAL firmware
 * function compiled on the host and compares the logs line for line. */
const path = require('path'), cp = require('child_process');
const root = path.join(__dirname, '..', '..', '..');
const Run = require('./tau_run.js'), Audio = require('./tau_audio.js');
const data = JSON.parse(cp.execFileSync('python3', ['-c', 'import sys,json;sys.path.insert(0,"tools/meters/preview");import build;print(json.dumps(build.data()))'], { cwd: root, maxBuffer: 1 << 26 }).toString());
const out = [];
const FRAMES = 90;
for (const key of ['winamp_bars', 'winamp_scope']) {
  const m = data.schema.meters.find((x) => x.key === key);
  for (let pi = 0; pi < m.presets.length; pi++) {
    const base = Audio.demo(3 + pi);
    // pause a few frames in the middle (the firmware zeroes targets / skips drawing while paused)
    const source = (n) => { const f = base(n); if (n >= 50 && n < 58) f.paused = true; return f; };
    const r = Run.run({ data, schema: data.schema, key, params: m.presets[pi].values, source, frames: FRAMES, themeIdx: 0, light: pi % 2 === 1, accentIdx: 1 + pi, log: true, barReady: true });
    const Bal = require('./tau_ballistics.js'), lvl = Bal.newSpec(), frames = [];
    for (let n = 0; n < FRAMES; n++) { const s = source(n); Bal.specStep(lvl, s.spec); frames.push({ paused: s.paused ? 1 : 0, spec: lvl.slice(), wave: s.wave }); }
    const box = { x: 16, y: 152, w: 368, h: 122 };
    out.push({ name: `${key}/${m.presets[pi].name}`, key, params: m.params.map((p) => r.params[p.key]), colors: { accent: r.theme.accent, prim: r.theme.role.text_primary, track: r.theme.role.surface_track, bg: r.theme.gradAt(box.y + (box.h >> 1)) }, box, frames, log: r.logs });
  }
}
process.stdout.write(JSON.stringify(out));
