/* Runs one meter over a source for N frames and returns the framebuffer and per-frame command counts. Shared by the browser lab, the node
 * tests and the golden-frame comparison (M3). */
(function (root) {
  const req = (n, g) => (typeof module !== 'undefined') ? require('./' + n) : root[g];
  const { Fb } = req('tau_fb.js', 'TauFb'), Th = req('tau_theme.js', 'TauTheme'), Bal = req('tau_ballistics.js', 'TauBallistics'), Cost = req('tau_cost.js', 'TauCost');
  const M = (key) => (typeof module !== 'undefined') ? require('./meters/' + key + '.js') : root.TauMeters[key];
  /* opts: {data, schema, key, params, source, frames, themeIdx, light, accentIdx, log, barReady} */
  function run(o) {
    const meterSchema = o.schema.meters.find((m) => m.key === o.key), mod = M(o.key);
    const theme = Th.makeTheme(o.data, o.themeIdx || 0, !!o.light, o.accentIdx == null ? 1 : o.accentIdx, 360);
    const p = Object.assign({}, ...meterSchema.params.map((x) => ({ [x.key]: x.default })), o.params || {});
    const fb = new Fb(400, 360, !!o.log); fb.barReady = o.barReady !== false;
    for (let y = 0; y < 360; y++) fb._fill(0, y, 400, 1, theme.gradAt(y));
    fb.cmds = 0; if (fb.log) fb.log.length = 0;
    const box = { x: 16, y: 152, w: 368, h: 122 }, bg = theme.gradAt(box.y + (box.h >> 1));
    const lvl = Bal.newSpec(), st = mod.state(), perFrame = [], logs = [];
    for (let n = 0; n < (o.frames == null ? 120 : o.frames); n++) {
      const src = o.source(n); if (src.post) { for (let b = 0; b < 16; b++) lvl[b] = src.spec[b]; } else Bal.specStep(lvl, src.spec);
      const c0 = fb.cmds, l0 = fb.log ? fb.log.length : 0;
      mod.tick({ fb, x: box.x, y: box.y, w: box.w, h: box.h, bg, theme, spec: lvl, wave: src.wave, paused: src.paused, p, st, force: n === 0 });
      perFrame.push(fb.cmds - c0);
      if (fb.log) logs.push(fb.log.slice(l0));
    }
    return { fb, perFrame, cost: Cost.summarize(perFrame, meterSchema.cost_class), theme, params: p, logs };
  }
  const api = { run };
  if (typeof module !== 'undefined') module.exports = api; else root.TauRun = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
