/* Cost panel: commands per frame against the meter's declared cost class (0: 40, 1: 100, 2: 400 commands, VIZ_BARS' 36 is the baseline).
 * Everything here is a MODEL estimate until a hardware Check has measured the meter (docs/METER_MODULE_SPEC.md section 7). */
(function (root) {
  const BUDGET = [40, 100, 400];
  function summarize(perFrame, costClass) {
    const n = perFrame.length, max = Math.max(...perFrame), mean = perFrame.reduce((a, b) => a + b, 0) / n;
    const budget = BUDGET[costClass == null ? 2 : costClass];
    return { frames: n, mean: Math.round(mean * 10) / 10, max, budget, over: max > budget, basis: 'model estimate', vsBars: Math.round(mean / 36 * 100) / 100 };
  }
  const api = { BUDGET, summarize };
  if (typeof module !== 'undefined') module.exports = api; else root.TauCost = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
