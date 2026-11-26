// Runs lab-twin calls (tools/meters/preview/tau_fb.js) case by case and writes selected engine-memory rows as raw little-endian
// 16-bit words to an output file.  usage: node tools/talos_v2_model/tau_fb_driver.js cases.json out.bin
//   cases.json = [{"ops": [["rect", x, y, w, h, c], ["clutLoad", start, [..]], ...], "rows": [y0, y1]}, ...]
// Each case starts from the probe pattern (word k = k * 0x9E37 + 0x1234 mod 2^16) over the 512 x 1024 engine memory
// (visible 400 x 360 frame plus the stash, pitch 512). Output: per case, the words of rows y0..y1-1 (512 per row).
const path = require('path');
const fs = require('fs');
const { Fb } = require(path.join(__dirname, '..', 'meters', 'preview', 'tau_fb.js'));   // repository copy (tools/meters/preview)
const cases = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const pat = (k) => (Math.imul(k, 0x9E37) + 0x1234) & 0xFFFF;
const fd = fs.openSync(process.argv[3], 'w');
for (const c of cases) {
  const fb = new Fb(400, 360, false);
  const st = fb._st();
  for (let k = 0; k < 512 * 1024; k++) st[k] = pat(k);
  for (let y = 0; y < 360; y++) for (let x = 0; x < 400; x++) fb.px[y * 400 + x] = pat(y * 512 + x);
  for (const [name, ...args] of c.ops) fb[name](...args);
  const out = new Uint16Array((c.rows[1] - c.rows[0]) * 512);
  let i = 0;
  for (let y = c.rows[0]; y < c.rows[1]; y++) for (let x = 0; x < 512; x++)
    out[i++] = (y < 360 && x < 400) ? fb.px[y * 400 + x] : st[y * 512 + x];
  fs.writeSync(fd, Buffer.from(out.buffer));
}
fs.closeSync(fd);
