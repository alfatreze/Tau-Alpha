#!/usr/bin/env node
/* Runs the golden scenarios of tools/meters/preview/golden_lw.js (the JS twin's command logs) through the LAYERED WAVE WebAssembly module (the firmware's own drawing source, wasm/layered_wave_wasm.c) and compares the engine
 * commands frame by frame: node parity_lw.js SCENARIOS.json. Exit 0 when every command is identical. (sim/test_layered_wave_wasm.py runs it.) */
const fs = require('fs'), path = require('path');
const W = require(path.join(__dirname, '..', 'meters', 'layered_wave_wasm.js'));
const ROLE_NAMES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar'];
const GOLDEN_ROLES = ['accent', 'text_primary', 'text_secondary', 'ok', 'warn', 'danger', 'pill', 'error', 'surface', 'surface_track', 'base', 'bg_bottom'];     // the order golden_lw.js writes them
const OFFS = ['x', 'y', 'w', 'h', 'bg', 'force', 'paused', 'degrade', 'frame', 'dt_ms', 'peak_l', 'peak_r'];
const WIDTH = { x: 2, y: 2, w: 2, h: 2, bg: 2, force: 1, paused: 1, degrade: 1, dt_ms: 2, frame: 4, peak_l: 4, peak_r: 4 };
const scen = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
WebAssembly.compile(new Uint8Array(Buffer.from(W.b64, 'base64'))).then((mod) => {
  let cur = null, total = 0, bad = 0;
  for (const s of scen) {
    const inst = new WebAssembly.Instance(mod, { env: {
      rect: (x, y, w, h, c) => cur.push('rect ' + [x, y, w, h, c].join(' ')),
      scroll_left: (x, y, w, h, d) => cur.push('scroll_left ' + [x, y, w, h, d].join(' ')),
      text: () => 0 } });
    const x = inst.exports, mem = () => x.memory.buffer, dv = () => new DataView(mem());
    const o = new Uint16Array(mem(), x.offs_ptr(), OFFS.length), off = Object.fromEntries(OFFS.map((k, i) => [k, o[i]]));
    const put = (k, v) => { const a = x.in_ptr() + off[k]; if (WIDTH[k] === 1) dv().setUint8(a, v & 255); else if (WIDTH[k] === 2) dv().setUint16(a, v & 65535, true); else dv().setUint32(a, v >>> 0, true); };
    GOLDEN_ROLES.forEach((k, i) => dv().setUint16(x.roles_ptr() + 2 * ROLE_NAMES.indexOf(k) * 1, s.roles[i], true));
    const setParams = (arr) => { if (arr.length !== x.n_params()) throw new Error(`${s.name}: ${arr.length} parameters, the module has ${x.n_params()}`); arr.forEach((v, i) => dv().setUint16(x.params_ptr() + 2 * i, v, true)); };
    setParams(s.params);
    for (let n = 0; n < s.frames.length; n++) {
      const f = s.frames[n];
      if (n === s.chg) setParams(s.params2);
      const bx = s.geoat >= 0 && n >= s.geoat ? (s.box2 || s.box) : s.box;
      put('x', bx.x); put('y', bx.y); put('w', bx.w); put('h', bx.h); put('bg', 0x18C3); put('force', (n === 0 || n === s.frc) ? 1 : 0); put('paused', f.paused); put('dt_ms', 26); put('degrade', 0); put('frame', n);
      new Uint8Array(mem(), x.spec_ptr(), 16).set(f.spec.map((v) => Math.max(0, Math.min(255, Math.trunc(v)))));
      new Int8Array(mem(), x.wave_ptr(), 64).set(f.wave.map((v) => Math.max(-128, Math.min(127, Math.trunc(v)))));
      cur = []; x.tick_raw();
      const want = s.log[n];
      total += want.length;
      if (cur.length !== want.length || cur.some((c, i) => c !== want[i])) {
        bad++; const i = cur.findIndex((c, j) => c !== want[j]);
        console.log(`MISMATCH ${s.name} frame ${n}: command ${i} twin '${want[i]}' vs wasm '${cur[i]}' (twin ${want.length} commands, wasm ${cur.length})`); break;
      }
    }
  }
  console.log(bad ? `${bad} scenarios differ` : `layered wave wasm: ${total} commands identical to the JS twin in ${scen.length} scenarios`);
  process.exit(bad ? 1 : 0);
});
