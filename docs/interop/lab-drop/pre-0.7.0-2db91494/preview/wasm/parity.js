/* Runs the SOUND MAP twin over a script (the format of sim/p2_sound_map_cmdlog_harness.c) and prints the command stream in the same text format: node parity.js SCRIPT.txt > log.txt */
const fs = require('fs'), path = require('path');
const { Fb } = require(path.join(__dirname, '..', 'tau_fb.js'));
const M = require(path.join(__dirname, '..', 'meters', 'sound_map.js'));
const NAMES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar', 'fs_red', 'fs_track'];
const KEYS = ['mode', 'gain', 'cell', 'span', 'x', 'y', 'w', 'h', 'bg', 'force', 'paused', 'degrade', 'frame', 'dt_ms', 'peak_l', 'peak_r', 'rms_l', 'rms_r', 'corr', 'centroid', 'crest', 'flux', 'onset', 'stats_ok'];
M.ready().then(() => {
  const out = []; let theme = null; const st = M.state(); let n = 0;
  for (const line of fs.readFileSync(process.argv[2], 'utf8').split('\n')) {
    const t = line.trim().split(/\s+/); if (!t[0]) continue;
    if (t[0] === 'R') { const v = t.slice(1).map(Number); theme = { accent: v[6], role: Object.fromEntries(NAMES.map((k, i) => [k, v[i]])) }; continue; }
    const v = t.slice(1).map(Number), f = Object.fromEntries(KEYS.map((k, i) => [k, v[i]])), spec = v.slice(KEYS.length, KEYS.length + 16);
    const fb = new Fb(400, 360, true);
    M.tick({ fb, st, theme, spec, ctx: null, x: f.x, y: f.y, w: f.w, h: f.h, bg: f.bg, paused: !!f.paused, force: !!f.force, p: { mode: f.mode, gain: f.gain, cell: f.cell, span: f.span }, raw: f });
    out.push('T ' + n++); for (const l of fb.log) out.push(l);
  }
  process.stdout.write(out.join('\n') + '\n');
}).catch((e) => { console.error(e); process.exit(1); });
