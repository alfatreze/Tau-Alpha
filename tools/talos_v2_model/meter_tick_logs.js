// ORACLE-SIDE helper (audit B-1181): the lab twin's per-tick host-call logs of the seven shipped packs, window and fullscreen,
// for the per-meter-tick engine cost estimate in run_cost_estimate.py. usage: node tools/talos_v2_model/meter_tick_logs.js data.json out.json
const path = require('path'), fs = require('fs');
const P = path.join(__dirname, '..', 'meters', 'preview');
const Run = require(path.join(P, 'tau_run.js')), Audio = require(path.join(P, 'tau_audio.js'));
const data = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const cases = [['winamp_bars', {}], ['winamp_scope', {}], ['chladni', {}], ['layered_wave', {}], ['vu_master', {}],
               ['waterfall_grid', { flow: 0 }], ['waterfall_grid', { flow: 1 }], ['sound_map', {}]];
const boxes = { window: { x: 16, y: 152, w: 368, h: 122 }, full: { x: 0, y: 0, w: 400, h: 360 } };
const out = [];
for (const [key, params] of cases) for (const [bn, box] of Object.entries(boxes)) {
  const r = Run.run({ data, schema: data.schema, key, params, source: Audio.demo(1), frames: 60, log: true, box });
  out.push({ key, params, box: bn, perFrame: r.perFrame, logs: r.logs.slice(10) });   // skip the first frames (full repaint)
}
fs.writeFileSync(process.argv[3], JSON.stringify(out));
