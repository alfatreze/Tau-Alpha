#!/usr/bin/env node
/* node tools/meters/preview/render.js --meter winamp_bars [--preset 1] [--theme 0] [--light] [--accent 1] [--frames 60] --out file.png
 * Renders one frame of a meter over the deterministic demo source (headless, no browser) with the firmware's own maths. */
const fs = require('fs'), path = require('path'), zlib = require('zlib'), cp = require('child_process');
const root = path.join(__dirname, '..', '..', '..');
const a = process.argv.slice(2), opt = (k, d) => { const i = a.indexOf('--' + k); return i < 0 ? d : a[i + 1]; };
const data = JSON.parse(cp.execFileSync('python3', ['-c', 'import sys,json;sys.path.insert(0,"tools/meters/preview");import build;print(json.dumps(build.data()))'], { cwd: root, maxBuffer: 1 << 26 }).toString());
const Run = require('./tau_run.js'), Audio = require('./tau_audio.js'), F = require('./tau_fb.js');
const key = opt('meter', 'winamp_bars'), m = data.schema.meters.find((x) => x.key === key), pi = +opt('preset', m.default_preset || 0);
const r = Run.run({ data, schema: data.schema, key, params: m.presets.length ? m.presets[pi].values : {}, source: Audio.demo(1), frames: +opt('frames', 60), themeIdx: +opt('theme', 0), light: a.includes('--light'), accentIdx: +opt('accent', 1) });
const w = 400, h = 360, rgba = r.fb.rgba(), raw = Buffer.alloc((w * 3 + 1) * h);
for (let y = 0; y < h; y++) { raw[y * (w * 3 + 1)] = 0; for (let x = 0; x < w; x++) { const o = y * (w * 3 + 1) + 1 + x * 3, i = (y * w + x) * 4; raw[o] = rgba[i]; raw[o + 1] = rgba[i + 1]; raw[o + 2] = rgba[i + 2]; } }
const crc = (b) => { let c, t = crc.t || (crc.t = Array.from({ length: 256 }, (_, n) => { c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; })); let v = 0xFFFFFFFF; for (const x of b) v = t[(v ^ x) & 255] ^ (v >>> 8); return (v ^ 0xFFFFFFFF) >>> 0; };
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
fs.writeFileSync(opt('out', 'meter.png'), Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
console.log(`${m.name}: ${r.cost.mean} cmds/frame mean, worst ${r.cost.max}`);
