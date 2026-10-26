/* sound_map: the lab's twin of SOUND MAP. It is NOT a port: the pack's own drawing source (fw2/ui/meter_sound_map.c) compiled to WebAssembly (wasm/sound_map_wasm.c, tools/meters/preview/wasm/build_wasm.py; the module is
 * committed as sound_map_wasm.js), so what the lab draws is what the Pocket draws, command for command (sim/test_sound_map_twin.py compares the command streams with the native code). The draw calls the pack makes are
 * imports that land in the lab's framebuffer (fb.rect / fb.bar / fb.text). ctx = the usual lab context {fb, x, y, w, h, bg, theme, spec, paused, p (params by key), st, force}, plus optional
 *   ctx.stats = {rms_l, rms_r, corr, peak_l, peak_r}   what the hardware statistics block would report (rms and peaks on the scale 0..24575, corr in Q8 -256..256); without it the meter runs as on a bitstream
 *                                                      without the block (stats_ok = 0: the field and the loudness use the peaks, which the lab approximates from the spectrum)
 *   ctx.dt_ms (default 26), ctx.degrade (0..2);  ctx.raw = {every mtr_in_t field} runs the pack exactly as given (the parity test) instead of preparing centroid, flux, onset and crest.
 * The module compiles asynchronously (a browser refuses a synchronous compile of more than a few KB): call ready() once; until it resolves a tick draws nothing. */
(function (root) {
  const isNode = typeof module !== 'undefined';
  const W = () => (isNode ? require('./sound_map_wasm.js') : root.TauSoundMapWasm);        // looked up when first used: in the lab page the scripts are concatenated in file order and the data file comes after this one
  const ROLES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar', 'fs_red', 'fs_track'];
  const PARAMS = ['mode', 'gain', 'cell', 'span'];                       // the manifest's order (meters/sound_map/meter.json, MPP_SMP_*)
  const OFFS = ['x', 'y', 'w', 'h', 'bg', 'force', 'paused', 'degrade', 'frame', 'dt_ms', 'peak_l', 'peak_r', 'rms_l', 'rms_r', 'corr', 'centroid', 'crest', 'flux', 'onset', 'stats_ok'];
  const WIDTH = { bg: 2, x: 2, y: 2, w: 2, h: 2, dt_ms: 2, rms_l: 2, rms_r: 2, corr: 2, force: 1, paused: 1, degrade: 1, centroid: 1, crest: 1, flux: 1, onset: 1, stats_ok: 1, frame: 4, peak_l: 4, peak_r: 4 };
  let compiled = null, compiling = null;
  function bytes() { return isNode ? new Uint8Array(Buffer.from(W().b64, 'base64')) : Uint8Array.from(atob(W().b64), (c) => c.charCodeAt(0)); }
  function ready() { if (!compiling) compiling = WebAssembly.compile(bytes()).then((m) => { compiled = m; return true; }); return compiling; }
  function state() { return { inst: null, cur: null, t: 0, frames: 0 }; }
  function instantiate(st) {
    const mem = () => st.inst.exports.memory.buffer;
    st.inst = new WebAssembly.Instance(compiled, { env: {
      rect: (x, y, w, h, c) => st.cur.fb.rect(x, y, w, h, c),
      bar: (x, y, w, h, lit, fg, bg) => st.cur.fb.bar(x, y, w, h, lit, fg, bg),
      text: (x, y, p, fg, bg) => { const u = new Uint8Array(mem()); let e = p; while (u[e]) e++; return st.cur.fb.text(x, y, String.fromCharCode(...u.subarray(p, e)), fg, bg); } } });
    const x = st.inst.exports, o = new Uint16Array(x.memory.buffer, x.offs_ptr(), OFFS.length);
    st.off = Object.fromEntries(OFFS.map((k, i) => [k, o[i]]));
    st.inP = x.in_ptr(); st.specP = x.spec_ptr(); st.parP = x.params_ptr(); st.roleP = x.roles_ptr();
  }
  function put(st, field, v) {
    const dv = new DataView(st.inst.exports.memory.buffer), a = st.inP + st.off[field];
    if (WIDTH[field] === 1) dv.setUint8(a, v & 255); else if (WIDTH[field] === 2) (field === 'corr' ? dv.setInt16(a, v, true) : dv.setUint16(a, v & 65535, true)); else dv.setUint32(a, v >>> 0, true);
  }
  function tick(ctx) {
    const st = ctx.st;
    if (!compiled) { ready(); return; }
    if (!st.inst) instantiate(st);
    st.cur = ctx;
    const x = st.inst.exports, dv = new DataView(x.memory.buffer);
    for (let i = 0; i < ROLES.length; i++) dv.setUint16(st.roleP + 2 * i, ROLES[i] === 'accent' ? ctx.theme.accent : (ctx.theme.role[ROLES[i]] || 0), true);
    PARAMS.forEach((k, i) => dv.setUint16(st.parP + 2 * i, ctx.p[k] == null ? 0 : ctx.p[k], true));
    const spec = new Uint8Array(x.memory.buffer, st.specP, 16); for (let b = 0; b < 16; b++) spec[b] = Math.max(0, Math.min(255, Math.trunc(ctx.spec[b])));
    if (ctx.raw) {                                            // exactly the given inputs (the parity test)
      for (const k of OFFS) put(st, k, ctx.raw[k] || 0);
      x.tick_raw(); return;
    }
    const dt = ctx.dt_ms || 26, s = ctx.stats, mean = ctx.spec.reduce((a, v) => a + v, 0) / 16, approx = Math.trunc(Math.min(24575, mean * 24575 / 255 * 1.4));
    const vals = { x: ctx.x, y: ctx.y, w: ctx.w, h: ctx.h, bg: ctx.bg, force: ctx.force ? 1 : 0, paused: ctx.paused ? 1 : 0, degrade: ctx.degrade || 0, frame: st.frames++, dt_ms: dt,
                   peak_l: s ? s.peak_l : approx, peak_r: s ? s.peak_r : approx, rms_l: s ? s.rms_l : 0, rms_r: s ? s.rms_r : 0, corr: s ? s.corr : 0, centroid: 0, crest: 0, flux: 0, onset: 0, stats_ok: s ? 1 : 0 };
    for (const k of OFFS) put(st, k, vals[k]);
    st.t += dt; x.tick(st.t);
  }
  const api = { key: 'sound_map', ready, state, tick, get wasm_sha256() { return W().sha256; } };
  if (isNode) module.exports = api; else (root.TauMeters = root.TauMeters || {}).sound_map = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
