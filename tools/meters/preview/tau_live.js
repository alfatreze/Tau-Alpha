/* Live audio source for the lab (browser only): a file or the microphone through a Web Audio analyser, turned into the device's meter inputs.
 * It is an APPROXIMATION of the hardware: 16 half-octave bands from 93.75 Hz up (the layout of tau_spec_bank.sv, scaled to the sample rate), levels
 * log-scaled to 0..255 over a 54 dB window like spec_lvl, plus a 64-sample signed waveform (-100..100). Use the deterministic demo source for any
 * comparison against firmware; use this one to judge how a meter looks with real music. */
(function (root) {
  const EDGES = Array.from({ length: 17 }, (_, k) => 93.75 * Math.pow(2, k / 2));
  function create() {
    let ac = null, an = null, node = null, el = null, kind = null, freq = null, time = null;
    const ensure = () => { if (ac) return; ac = new (root.AudioContext || root.webkitAudioContext)(); an = ac.createAnalyser(); an.fftSize = 2048; an.smoothingTimeConstant = 0; freq = new Float32Array(an.frequencyBinCount); time = new Float32Array(an.fftSize); };
    const drop = () => { try { if (node) node.disconnect(); } catch (e) {} if (el) { el.pause(); el.src = ''; el = null; } try { an.disconnect(); } catch (e) {} node = null; kind = null; };
    async function useFile(file, loop) {
      ensure(); drop(); await ac.resume();
      el = new Audio(); el.src = URL.createObjectURL(file); el.loop = loop !== false; el.crossOrigin = 'anonymous';
      node = ac.createMediaElementSource(el); node.connect(an); an.connect(ac.destination); kind = 'file'; await el.play();
    }
    async function useMic() {
      ensure(); drop(); await ac.resume();
      const s = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false } });
      node = ac.createMediaStreamSource(s); node.connect(an); kind = 'mic';
    }
    function frame() {
      const spec = new Array(16).fill(0), wave = new Array(64).fill(0);
      if (!an || !kind) return { spec, wave, paused: true };
      if (el && el.paused) return { spec, wave, paused: true };
      an.getFloatFrequencyData(freq); an.getFloatTimeDomainData(time);
      const sr = ac.sampleRate, binHz = sr / an.fftSize, scale = sr / 48000;
      for (let k = 0; k < 16; k++) {
        const lo = Math.max(1, Math.round(EDGES[k] * scale / binHz)), hi = Math.max(lo + 1, Math.round(EDGES[k + 1] * scale / binHz));
        let s = 0; for (let b = lo; b < hi && b < freq.length; b++) { const m = Math.pow(10, freq[b] / 20); s += m * m; }
        const db = 20 * Math.log10(Math.sqrt(s) * 2.8 + 1e-9);
        spec[k] = Math.max(0, Math.min(255, Math.round((db + 54) / 54 * 255)));
      }
      const step = time.length / 64;
      for (let i = 0; i < 64; i++) { let v = 0; for (let j = 0; j < step; j++) { const x = time[Math.floor(i * step + j)]; if (Math.abs(x) > Math.abs(v)) v = x; } wave[i] = Math.max(-100, Math.min(100, Math.round(v * 110))); }
      return { spec, wave, paused: false };
    }
    const setLoop = (v) => { if (el) { el.loop = !!v; if (v && el.ended) el.play(); } };
    return { setLoop, useFile, useMic, frame, kind: () => kind };
  }
  const api = { create };
  if (typeof module !== 'undefined') module.exports = api; else root.TauLive = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
