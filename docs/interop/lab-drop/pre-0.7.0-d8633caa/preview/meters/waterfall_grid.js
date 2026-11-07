/* waterfall_grid: the lab's twin of WATERFALL GRID. It is NOT a port: the pack's own drawing source (fw2/ui/meter_waterfall_grid.c) compiled to WebAssembly (wasm/waterfall_grid_wasm.c,
 * tools/meters/preview/wasm/build_wasm.py; the module is committed as waterfall_grid_wasm.js), so what the lab draws is what the Pocket draws, command for command (sim/test_waterfall_golden.py compares the command
 * streams with the native C). The engine calls the pack makes are imports that land in the lab's framebuffer (fb.rect, fb.rectStash, fb.clutLoad, fb.cblit, fb.scrollDown, fb.scrollLeft).
 * ctx = the usual lab context {fb, x, y, w, h, bg, theme, spec[16] (0..255), paused, p (parameters by key), st, force}, plus optional ctx.dt_ms (default 26) and ctx.degrade (0..2).
 * The module compiles asynchronously in a browser (call ready() once; until then a tick draws nothing); in node the compile is synchronous. */
(function (root) {
  const isNode = typeof module !== 'undefined';
  const W = () => (isNode ? require('./waterfall_grid_wasm.js') : root.TauWaterfallGridWasm);        // looked up when first used: in the lab page the data file comes after this one
  const ROLES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar', 'fs_red', 'fs_track'];     // the TR_* order of fw2/ui/theme.h
  const OFFS = ['x', 'y', 'w', 'h', 'bg', 'force', 'paused', 'degrade', 'frame', 'dt_ms', 'peak_l', 'peak_r'];
  const WIDTH = { x: 2, y: 2, w: 2, h: 2, bg: 2, force: 1, paused: 1, degrade: 1, dt_ms: 2, frame: 4, peak_l: 4, peak_r: 4 };
  let compiled = null, compiling = null;
  const bytes = () => (isNode ? new Uint8Array(Buffer.from(W().b64, 'base64')) : Uint8Array.from(atob(W().b64), (c) => c.charCodeAt(0)));
  function ready() { if (!compiling) compiling = WebAssembly.compile(bytes()).then((m) => { compiled = m; return true; }); return compiling; }
  function state() { return { inst: null, cur: null, frames: 0 }; }
  function instantiate(st) {
    if (!compiled) { if (!isNode) return false; compiled = new WebAssembly.Module(bytes()); }
    st.inst = new WebAssembly.Instance(compiled, { env: {
      rect: (x, y, w, h, c) => st.cur.fb.rect(x, y, w, h, c),
      rect_stash: (x, y, w, h, c) => st.cur.fb.rectStash(x, y, w, h, c),
      clut_load: (start, p, n) => st.cur.fb.clutLoad(start, new Uint16Array(st.inst.exports.memory.buffer.slice(p, p + 2 * n))),
      cblit: (sx, sy, dx, dy, w, h) => st.cur.fb.cblit(sx, sy, dx, dy, w, h),
      scroll_down: (x, y, w, h, d) => st.cur.fb.scrollDown(x, y, w, h, d),
      scroll_left: (x, y, w, h, d) => st.cur.fb.scrollLeft(x, y, w, h, d),
      text: () => 0 } });
    const x = st.inst.exports, o = new Uint16Array(x.memory.buffer, x.offs_ptr(), OFFS.length);
    st.off = Object.fromEntries(OFFS.map((k, i) => [k, o[i]]));
    return true;
  }
  function put(st, field, v) {
    const dv = new DataView(st.inst.exports.memory.buffer), a = st.inst.exports.in_ptr() + st.off[field];
    if (WIDTH[field] === 1) dv.setUint8(a, v & 255); else if (WIDTH[field] === 2) dv.setUint16(a, v & 65535, true); else dv.setUint32(a, v >>> 0, true);
  }
  function tick(ctx) {
    const st = ctx.st;
    if (!st.inst && !instantiate(st)) { ready(); return; }          // a browser draws nothing until the module is compiled
    st.cur = ctx;
    const x = st.inst.exports, mem = x.memory.buffer, dv = new DataView(mem), keys = W().params;
    if (keys.length !== x.n_params()) throw new Error('waterfall_grid: the module has ' + x.n_params() + ' parameters, its key list ' + keys.length);
    const th = ctx.theme;
    ROLES.forEach((k, i) => dv.setUint16(x.roles_ptr() + 2 * i, k === 'accent' ? th.accent : (th.role[k] || 0), true));
    keys.forEach((k, i) => dv.setUint16(x.params_ptr() + 2 * i, ctx.p[k] == null ? 0 : ctx.p[k] & 65535, true));
    new Uint8Array(mem, x.spec_ptr(), 16).set(Array.from(ctx.spec, (v) => Math.max(0, Math.min(255, Math.trunc(v)))));
    const vals = { x: ctx.x, y: ctx.y, w: ctx.w, h: ctx.h, bg: ctx.bg || 0, force: ctx.force ? 1 : 0, paused: ctx.paused ? 1 : 0, degrade: ctx.degrade || 0, frame: st.frames++, dt_ms: ctx.dt_ms || 26, peak_l: 0, peak_r: 0 };
    for (const k of OFFS) put(st, k, vals[k]);
    x.tick_raw();
  }
  const api = { key: 'waterfall_grid', ready, state, tick, get wasm_sha256() { return W().sha256; } };
  if (isNode) module.exports = api; else (root.TauMeters = root.TauMeters || {}).waterfall_grid = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
