/* tau_host: the lab's messages with the page that embeds it (Tau Omega's preset editor; Tau-Alpha message 009, confirmed in Omega 011). Pure and testable in node (sim/test_lab_host.py); index.html only wires it to window messages.
 *
 *   embedder -> lab   {type:"tau-lab-set", meter:<manifest id>, values:{<parameter key>:<integer>}}
 *   lab -> embedder   {type:"tau-lab-ready", meters:[{id, key, wasm_sha256}]}      once every module has compiled (layered_wave also has wasm_sha256_timed)
 *   lab -> embedder   {type:"tau-lab-applied", meter, ignored:[key...], clamped:{key:appliedValue}, values:{every parameter as the lab now holds it}}   (+ error:"unknown meter" when the id has no module)
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
  function readyPayload(S, T) {
    const meters = [];
    for (const m of S.meters) {
      const a = T[m.key]; if (!a) continue;
      const e = { id: m.id, key: m.key, wasm_sha256: a.wasm_sha256 || null };
      if (a.wasm_sha256_timed) e.wasm_sha256_timed = a.wasm_sha256_timed;
      meters.push(e);
    }
    return { type: 'tau-lab-ready', meters };
  }
  const api = { plan, readyPayload, find };
  if (typeof module !== 'undefined') module.exports = api; else root.TauHost = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
