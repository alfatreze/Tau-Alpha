/* chladni: the lab's twin of CHLADNI: the pack's own source (fw2/ui/meter_chladni.c) compiled to WebAssembly (wasm/chladni_wasm.c; owner decision D-M15), through the shared adapter _wasm_meter.js. The SDRAM mailbox, the
 * engine's copy inside its memory and the scaled blit are modelled by the lab's framebuffer (tau_fb.js: mbWrite, mbRead, blitAbs, sblit, fence). It draws a figure every CHL_PERIOD_MS of the input's dt_ms (ctx.dt_ms, 26). */
(function (root) {
  const WM = typeof module !== 'undefined' ? require('./_wasm_meter.js') : root.TauWasmMeter;
  const api = WM.make({ key: 'chladni', global: 'TauChladniWasm' });
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).chladni = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
