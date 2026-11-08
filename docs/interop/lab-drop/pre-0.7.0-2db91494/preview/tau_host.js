/* tau_host: the lab's messages with the page that embeds it (Tau Omega's preset editor; Tau-Alpha message 009, confirmed in Omega 011). Pure and testable in node (sim/test_lab_host.py); index.html only wires it to window messages.
 *
 *   embedder -> lab   {type:"tau-lab-set", meter:<manifest id>, values:{<parameter key>:<integer>}}
 *   lab -> embedder   {type:"tau-lab-ready", meters:[{id, key, wasm_sha256}]}      once every module has compiled (layered_wave also has wasm_sha256_timed)
 *   lab -> embedder   {type:"tau-lab-applied", meter, ignored:[key...], clamped:{key:appliedValue}, values:{every parameter as the lab now holds it}}   (+ error:"unknown meter" when the id has no module)
 *
 *
 * CONTRACT 1 (docs/features/LAB_EMBED_CONTRACT.md), added for Tau Omega message 014 (R-1 to R-5):
 *   embedder -> lab   {type:"tau-lab-embed", on:true|false}           embed mode: no heading, panel or parameter list, transparent, the canvas cropped to the current view and drawn at its own pixel size
 *                                                                      (the page also starts in embed mode when its URL fragment is #embed)
 *   lab -> embedder   {type:"tau-lab-size", view:"small"|"full", rect:{x,y,w,h}, w, h}      the meter view's size (the crop in embed mode), sent when it changes
 *   lab -> embedder   {type:"tau-lab-options", meter, options:[{id,label,type:"enum"|"bool"|"range",choices|min,max,value,applies_to:[meter id...]|null}]}   after tau-lab-ready, after every tau-lab-set and tau-lab-option
 *   embedder -> lab   {type:"tau-lab-option", id, value}              answered with {type:"tau-lab-option-applied", id, value, clamped:bool} or {..., error:"unknown option"|"not a number"|"not for this meter"}
 *   lab -> embedder   {type:"tau-lab-cost", meter, frame, window:{n,mean,max}, recent:{n,mean,max}, budget, class, over}   every other frame (window = the last 120 frames, the VERDICT window: over = window.max > budget; recent = the last 30)
 *   lab -> embedder   {type:"tau-lab-info", meter, cost:{...fields...}, table:[{key,label,options:[{value,selected,cpu_low,cpu_high,delta,kops}]}]}   after setup and each change (about 0.5 s later), and on {type:"tau-lab-info-get"}
 * The per-meter views (small and full rectangles, which meters have which) are in lab-data.json `views`.
 *
 * The rule is the firmware's preset rule (fw2/cores/mpreset_core.h): a preset is SPARSE, so a parameter the message does not name takes the meter's default; a key the meter does not have is ignored (listed in `ignored`); a value is clamped
 * to the parameter's range (enum 0 to n-1, bool 0 to 1, numbers min to max; `clamped` holds what the lab applied instead). A value that is not an integer is ignored. */
(function (root) {
  function range(x) {
    if (x.type === 'enum') return [0, x.values.length - 1];
    if (x.type === 'bool') return [0, 1];
    return [x.min, x.max];
  }
  function find(S, T, id) {                                   // the meter by manifest id, only if the page has its module
    const m = S.meters.find((x) => String(x.id) === String(id));
    return m && T[m.key] ? m : null;
  }
  function plan(S, T, id, values) {
    const reply = { type: 'tau-lab-applied', meter: id, ignored: [], clamped: {}, values: {} };
    const given = values && typeof values === 'object' && !Array.isArray(values) ? values : {};
    const m = find(S, T, id);
    if (!m) { reply.ignored = Object.keys(given); reply.error = 'unknown meter'; return { meter: null, full: null, reply }; }
    const known = new Set(m.params.map((x) => x.key));
    for (const k of Object.keys(given)) if (!known.has(k) || !Number.isInteger(given[k])) reply.ignored.push(k);
    const full = {};
    for (const x of m.params) {
      let v = x.default;
      if (Object.prototype.hasOwnProperty.call(given, x.key) && Number.isInteger(given[x.key])) {
        const [lo, hi] = range(x), g = given[x.key];
        v = Math.min(hi, Math.max(lo, g));
        if (v !== g) reply.clamped[x.key] = v;
      }
      full[x.key] = v;
    }
    reply.values = Object.assign({}, full);
    return { meter: m, full, reply };
  }
  const WINDOW = 120, RECENT = 30;                            // the cost verdict window (the page's own, about 3 s at 38 Hz) and the short one
  function stat(a) {
    const n = a.length; if (!n) return { n: 0, mean: 0, max: 0 };
    return { n, mean: Math.round(a.reduce((x, y) => x + y, 0) / n * 10) / 10, max: Math.max(...a) };
  }
  function costClass(m) { return m && m.cost_class != null ? m.cost_class : 2; }   // a meter with no class counts as the largest, in every message (the lab's BUDGET lookup does the same)
  function costWindows(costs, costClass, budgets) {           // costs: commands per frame, oldest first
    const w = costs.slice(-WINDOW), budget = budgets[costClass == null ? 2 : costClass];
    const win = stat(w);
    return { frame: costs.length ? costs[costs.length - 1] : 0, window: win, recent: stat(costs.slice(-RECENT)), budget, class: costClass == null ? 2 : costClass, over: win.max > budget };
  }
  function optionDefs(S, D) {                                 // the preview controls, as data; `dom` (the page's own control values) is not sent
    const id = (key) => { const m = S.meters.find((x) => x.key === key); return m ? m.id : null; };
    const withFull = S.meters.filter((m) => D.views && D.views.meters[String(m.id)] && D.views.meters[String(m.id)].full).map((m) => m.id);
    return [
      { id: 'theme', label: 'Theme', type: 'enum', choices: D.themes.map((t) => t.name), applies_to: null },
      { id: 'mode', label: 'Mode', type: 'enum', choices: ['Dark', 'Light'], applies_to: null },
      { id: 'accent', label: 'Accent', type: 'enum', choices: D.palette.map((p) => p.name), applies_to: null },
      { id: 'src', label: 'Source', type: 'enum', choices: ['Demo (deterministic)', 'Sweep', 'Silence'], applies_to: null, dom: ['demo', 'sweep', 'silence'] },
      { id: 'fullfig', label: 'Full screen view', type: 'bool', applies_to: withFull },
      { id: 'barready', label: 'Bars use OP_BAR', type: 'bool', applies_to: [id('winamp_bars')].filter((x) => x !== null) },
      { id: 'timedg', label: 'CPU-time guard (model)', type: 'bool', applies_to: [id('layered_wave')].filter((x) => x !== null) },
      { id: 'statsok', label: 'Statistics block', type: 'bool', applies_to: [id('sound_map')].filter((x) => x !== null) },
      { id: 'corr', label: 'Correlation', type: 'range', min: -256, max: 256, applies_to: [id('sound_map')].filter((x) => x !== null) },
    ];
  }
  function wireOptions(defs, values, meter) {                 // for one meter: the options that apply to it, with the current value of each
    return defs.filter((d) => d.applies_to === null || (meter !== null && meter !== undefined && d.applies_to.some((x) => String(x) === String(meter))))
      .map((d) => { const o = { id: d.id, label: d.label, type: d.type, value: values[d.id], applies_to: d.applies_to }; if (d.choices) o.choices = d.choices; if (d.type === 'range') { o.min = d.min; o.max = d.max; } return o; });
  }
  function checkOption(defs, meter, id, value) {              // -> {ok, value, clamped} | {ok:false, error}
    const d = defs.find((x) => x.id === id);
    if (!d) return { ok: false, error: 'unknown option' };
    if (d.applies_to !== null && !d.applies_to.some((x) => String(x) === String(meter))) return { ok: false, error: 'not for this meter' };
    if (d.type === 'bool') { if (value === true || value === 1) return { ok: true, value: 1, clamped: false }; if (value === false || value === 0) return { ok: true, value: 0, clamped: false }; return { ok: false, error: 'not a number' }; }
    if (!Number.isInteger(value)) return { ok: false, error: 'not a number' };
    const [lo, hi] = d.type === 'enum' ? [0, d.choices.length - 1] : [d.min, d.max];
    const v = Math.min(hi, Math.max(lo, value));
    return { ok: true, value: v, clamped: v !== value };
  }
  function readyPayload(S, T, contract) {
    const meters = [];
    for (const m of S.meters) {
      const a = T[m.key]; if (!a) continue;
      const e = { id: m.id, key: m.key, wasm_sha256: a.wasm_sha256 || null };
      if (a.wasm_sha256_timed) e.wasm_sha256_timed = a.wasm_sha256_timed;
      meters.push(e);
    }
    const r = { type: 'tau-lab-ready', meters }; if (contract !== undefined) r.contract = contract;
    return r;
  }
  const api = { plan, readyPayload, find, costClass, costWindows, optionDefs, wireOptions, checkOption, WINDOW, RECENT };
  if (typeof module !== 'undefined') module.exports = api; else root.TauHost = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
