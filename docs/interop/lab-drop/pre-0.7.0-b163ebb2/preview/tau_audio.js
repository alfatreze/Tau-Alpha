/* Audio sources for the lab. Everything produces the device's meter inputs, not audio: 16 band levels (0..255, before the
 * firmware's ballistics) and a 64-sample signed waveform (-100..100). The default is a deterministic synthetic source so the same
 * trace replays identically in the browser, in node tests and against the C code (docs/METER_MODULE_SPEC.md section 7). */
(function (root) {
  function rng(seed) { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; }
  /* Deterministic demo: a beat every ~0.5 s that lifts the low bands, a slow sweep across the mids, some noise. frame = 26 ms tick. */
  function demo(seed) {
    return function frame(n) {
      const r = rng(Math.imul(seed || 1, 1000003) + n * 7919);   // pure in n: any frame can be recomputed, a run can be replayed
      const t = n * 0.026, beat = Math.max(0, 1 - ((t % 0.5) / 0.5) * 2.2);
      const spec = [], wave = [];
      for (let b = 0; b < 16; b++) {
        const low = Math.max(0, 1 - b / 7), sweep = Math.exp(-Math.pow((b - (8 + 6 * Math.sin(t * 0.7))) / 2.2, 2));
        let v = 40 + 190 * (beat * low * 0.9 + sweep * 0.5) + 25 * (r() - 0.5);
        spec.push(Math.max(0, Math.min(255, Math.round(v))));
      }
      for (let i = 0; i < 64; i++) wave.push(Math.max(-100, Math.min(100, Math.round(60 * Math.sin(t * 40 + i * 0.35 * (1 + 0.5 * Math.sin(t))) * (0.4 + 0.6 * beat) + 12 * (r() - 0.5)))));
      return { spec, wave, paused: false };
    };
  }
  function silence() { return () => ({ spec: new Array(16).fill(0), wave: new Array(64).fill(0), paused: true }); }
  function sweep() {
    return (n) => { const spec = []; const c = (n * 0.15) % 16; for (let b = 0; b < 16; b++) spec.push(Math.round(255 * Math.exp(-Math.pow((b - c) / 1.5, 2)))); const wave = []; for (let i = 0; i < 64; i++) wave.push(Math.round(80 * Math.sin(i * (0.2 + c * 0.05)))); return { spec, wave, paused: false }; };
  }
  /* A hardware trace holds spec_lvl AFTER the firmware ballistics, so `post` tells the runner not to apply them again. */
  function trace(tr) { return (n) => { const f = tr.frames[n % tr.frames.length]; return { spec: f.spec, wave: f.wave, paused: !!f.paused, post: true }; }; }
  const api = { demo, silence, sweep, trace, rng };
  if (typeof module !== 'undefined') module.exports = api; else root.TauAudio = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
