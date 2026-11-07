/* vu_master: the lab's twin of MASTER VU: the pack's own source (fw2/ui/meter_vu_master.c) compiled to WebAssembly (wasm/vu_master_wasm.c; owner decision D-M15), through the shared adapter _wasm_meter.js.
 * The two peaks it shows come from ctx.vu = {l, r} (0..24575), else ctx.stats.peak_l / peak_r, else an estimate from the spectrum (the mean level scaled as the Sound Map's lab path does). */
(function (root) {
  const WM = typeof module !== 'undefined' ? require('./_wasm_meter.js') : root.TauWasmMeter;
  const api = WM.make({ key: 'vu_master', global: 'TauVuMasterWasm', inputs(ctx, x, vals) {
    const s = ctx.stats, v = ctx.vu, mean = ctx.spec.reduce((a, b) => a + b, 0) / ctx.spec.length, est = Math.trunc(Math.min(24575, mean * 24575 / 255 * 1.4));
    vals.peak_l = v ? v.l : s ? s.peak_l : est; vals.peak_r = v ? v.r : s ? s.peak_r : est;
  } });
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).vu_master = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
