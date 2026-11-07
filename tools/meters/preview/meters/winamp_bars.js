/* winamp_bars: the lab's twin of WINAMP BARS: the pack's own source (fw2/ui/meter_winamp_bars.c) compiled to WebAssembly (wasm/winamp_bars_wasm.c; owner decision D-M15), through the shared adapter _wasm_meter.js. */
(function (root) {
  const WM = typeof module !== 'undefined' ? require('./_wasm_meter.js') : root.TauWasmMeter;
  const api = WM.make({ key: 'winamp_bars', global: 'TauWinampBarsWasm' });
  if (typeof module !== 'undefined') module.exports = api; else (root.TauMeters = root.TauMeters || {}).winamp_bars = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
