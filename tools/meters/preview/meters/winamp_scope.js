/* winamp_scope: the lab's twin of WINAMP SCOPE: the pack's own source (fw2/ui/meter_winamp_scope.c) compiled to WebAssembly (wasm/winamp_scope_wasm.c; owner decision D-M15), through the shared adapter _wasm_meter.js.
 * The hardware capture block it reads through registers is the module's `cap_ptr()` buffer: a frame's capture is `ctx.capture` (256 [min, max] pairs of signed 16-bit values); the lab derives one from the demo waveform
 * (capture(wave): each hardware column spans two samples of a linear interpolation, scaled to the 16-bit range), a recorded trace supplies the waveform only. `ctx.fb.blendReady` false = the wipe path. */
(function (root) {
  const WM = typeof module !== 'undefined' ? require('./_wasm_meter.js') : root.TauWasmMeter;
  const COLS = 64, HW = 256;
  function capture(wave) {
    const out = [];
    for (let k = 0; k < HW; k++) {
      const a = wave[k >> 2] * 300, b = wave[Math.min(COLS - 1, (k >> 2) + ((k & 3) >= 2 ? 1 : 0))] * 300;
      out.push([Math.min(a, b), Math.max(a, b)]);
    }
    return out;
  }
  const api = WM.make({ key: 'winamp_scope', global: 'TauWinampScopeWasm', inputs(ctx, x) {
    const cap = ctx.capture || capture(ctx.wave || new Array(COLS).fill(0)), buf = new Uint32Array(x.memory.buffer, x.cap_ptr(), HW);
    for (let k = 0; k < HW; k++) buf[k] = (((cap[k][0] & 0xFFFF) << 16) | (cap[k][1] & 0xFFFF)) >>> 0;
  } });
  api.capture = capture;
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).winamp_scope = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
