/* Cost panel: commands per frame against the meter's declared cost class (0: 40, 1: 100, 2: 400 commands, VIZ_BARS' 36 is the baseline).
 * Everything here is a MODEL estimate until a hardware Check has measured the meter (docs/METER_MODULE_SPEC.md section 7). */
(function (root) {
  const BUDGET = [40, 100, 400];
  function summarize(perFrame, costClass) {
    const n = perFrame.length, max = Math.max(...perFrame), mean = perFrame.reduce((a, b) => a + b, 0) / n;
    const budget = BUDGET[costClass == null ? 2 : costClass];
    return { frames: n, mean: Math.round(mean * 10) / 10, max, budget, over: max > budget, basis: 'model estimate', vsBars: Math.round(mean / 36 * 100) / 100 };
  }
  /* CPU estimate (MODEL, unmeasured): issuing a command costs 60-400 cycles (60 = the MMIO pushes alone, 400 = with FIFO waits), one column/band
     evaluation 30-80 cycles in fixed point, at the ~38 Hz UI cadence on a 66.667 MHz clk_sys. A range, because nothing here has been measured on a
     Pocket; the real number comes from the meter sweep Check (CPU LOAD, DRAW STALL). `evals` = per-frame evaluations a meter reports in st.work. */
  const FPS = 38, CLK = 66.667e6, CYC_CMD = [60, 400], CYC_EVAL = [30, 80];
  function cpu(cmds, evals) { const f = (i) => Math.round((cmds * CYC_CMD[i] + (evals || 0) * CYC_EVAL[i]) * FPS / CLK * 1000) / 10; return { low: f(0), high: f(1) }; }
  const api = { BUDGET, summarize, cpu };
  if (typeof module !== 'undefined') module.exports = api; else root.TauCost = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
