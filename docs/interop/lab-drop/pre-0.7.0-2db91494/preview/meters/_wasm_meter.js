/* _wasm_meter: the lab's adapter factory for the Player 2 meters that are WebAssembly modules of the firmware's own drawing source (owner decision D-M15; see docs/features/meters/METER_WASM_MODULES.md). One module per meter
 * (meters/<key>_wasm.js, built by wasm/build_wasm.py), one tiny adapter per meter (meters/<key>.js) made here: make({ key, global, inputs }) returns { key, ready, state, tick }. The engine calls a module makes are imports
 * that land in the lab's framebuffer (tau_fb.js); the one environment below serves every module (a module imports only what its meter calls). ctx = the lab context {fb, x, y, w, h, bg, theme, spec[16], paused, p
 * (parameters by key), st, force}, plus optional ctx.dt_ms (default 26) and ctx.degrade. `inputs(ctx, x, vals)` may set more of the module's input per picture (the Scope's capture, the VU's peaks). The module compiles
 * asynchronously in a browser (call ready() once; until then a tick draws nothing); in node the compile is synchronous. The name starts with an underscore so the lab page loads it before the adapters. */
(function (root) {
  const isNode = typeof module !== 'undefined';
  const ROLES = ['bg_top', 'bg_bottom', 'surface', 'surface_track', 'text_primary', 'text_secondary', 'accent', 'on_accent', 'accent2', 'ok', 'warn', 'danger', 'base', 'chrome', 'pill', 'error', 'faint', 'splash_bg', 'splash_bar', 'fs_red', 'fs_track'];     // the TR_* order of fw2/ui/theme.h
  const OFFS = ['x', 'y', 'w', 'h', 'bg', 'force', 'paused', 'degrade', 'frame', 'dt_ms', 'peak_l', 'peak_r'];
  const WIDTH = { x: 2, y: 2, w: 2, h: 2, bg: 2, force: 1, paused: 1, degrade: 1, dt_ms: 2, frame: 4, peak_l: 4, peak_r: 4 };
  function make(cfg) {
    const W = () => (isNode ? require('./' + cfg.key + '_wasm.js') : root[cfg.global]);        // looked up when first used: in the lab page the data file comes after the adapters
    let compiled = null, compiling = null;
    const bytes = () => (isNode ? new Uint8Array(Buffer.from(W().b64, 'base64')) : Uint8Array.from(atob(W().b64), (c) => c.charCodeAt(0)));
    function ready() { if (!compiling) compiling = WebAssembly.compile(bytes()).then((m) => { compiled = m; return true; }); return compiling; }
    function state() { return { inst: null, cur: null, frames: 0 }; }
    function instantiate(st) {
      if (!compiled) { if (!isNode) return false; compiled = new WebAssembly.Module(bytes()); }
      const F = () => st.cur.fb, mem = () => st.inst.exports.memory.buffer;
      const cstr = (p) => { const u = new Uint8Array(mem()); let e = p; while (u[e]) e++; return String.fromCharCode(...u.subarray(p, e)); };
      st.inst = new WebAssembly.Instance(compiled, { env: {
        rect: (x, y, w, h, c) => F().rect(x, y, w, h, c),
        bar: (x, y, w, h, lit, fg, bg) => F().bar(x, y, w, h, lit, fg, bg),
        text: (x, y, p, fg, bg) => F().text(x, y, cstr(p), fg, bg),
        blit: (sx, sy, dx, dy, w, h) => F().blit(sx, sy, dx, dy, w, h),
        blit_abs: (sx, sy, dx, dy, w, h) => F().blitAbs(sx, sy, dx, dy, w, h),
        sblit: (sx, sy, dx, dy, w, h, s) => F().sblit(sx, sy, dx, dy, w, h, s),
        rect_stash: (x, y, w, h, c) => F().rectStash(x, y, w, h, c),
        blend_ready: () => (F().blendReady ? 1 : 0),
        blend_on: (a) => F().blendOn(a), blend_off: () => F().blendOff(),
        blend_blit: (sx, sy, dx, dy, w, h) => F().blendBlit(sx, sy, dx, dy, w, h),
        mb_write: (a, d) => F().mbWrite(a, d), mb_read: (a) => F().mbRead(a), fence: () => F().fence() } });
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
      const x = st.inst.exports, mem = x.memory.buffer, dv = new DataView(mem), keys = W().params, th = ctx.theme;
      if (keys.length !== x.n_params()) throw new Error(cfg.key + ': the module has ' + x.n_params() + ' parameters, its key list ' + keys.length);
      ROLES.forEach((k, i) => dv.setUint16(x.roles_ptr() + 2 * i, k === 'accent' ? th.accent : (th.role[k] || 0), true));
      keys.forEach((k, i) => dv.setUint16(x.params_ptr() + 2 * i, ctx.p[k] == null ? 0 : ctx.p[k] & 65535, true));
      new Uint8Array(mem, x.spec_ptr(), 16).set(Array.from(ctx.spec, (v) => Math.max(0, Math.min(255, Math.trunc(v)))));
      const vals = { x: ctx.x, y: ctx.y, w: ctx.w, h: ctx.h, bg: ctx.bg || 0, force: ctx.force ? 1 : 0, paused: ctx.paused ? 1 : 0, degrade: ctx.degrade || 0, frame: st.frames++, dt_ms: ctx.dt_ms || 26, peak_l: 0, peak_r: 0 };
      if (cfg.inputs) cfg.inputs(ctx, x, vals);
      for (const k of OFFS) put(st, k, vals[k]);
      x.tick_raw();
    }
    return { key: cfg.key, ready, state, tick, get wasm_sha256() { return W().sha256; } };
  }
  const api = { make };
  if (isNode) module.exports = api; else root.TauWasmMeter = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
