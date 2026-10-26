/* The firmware's spectrum ballistics (fw/player.c, hardware spectrum path): instant attack, release by a quarter of the gap plus one
 * per 26 ms tick. Turns a target (0..255 per band) into spec_lvl[]. */
(function (root) {
  function newSpec() { return new Array(16).fill(0); }
  function specStep(lvl, target) {
    for (let b = 0; b < 16; b++) {
      const v = target[b];
      if (v >= lvl[b]) lvl[b] = v; else lvl[b] -= Math.trunc((lvl[b] - v) / 4) + 1;
    }
    return lvl;
  }
  const api = { newSpec, specStep };
  if (typeof module !== 'undefined') module.exports = api; else root.TauBallistics = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
