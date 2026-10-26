/* Theme roles, the accent rule and the background ramp exactly as fw/player.c builds them (th_accent_of, ui_grad_set, ui_grad_at).
 * data = { themes:[{name, dark:{role keys..., bg_luma}, light:{...}}], palette:[{name, c565}], lightAccMaxL } from build.py. */
(function (root) {
  const tdiv = (a, b) => Math.trunc(a / b);
  const snap = (v) => {
    if (typeof v === 'string' && v.toLowerCase().startsWith('0x')) return parseInt(v, 16);
    const s = v.replace('#', ''); const r = parseInt(s.slice(0, 2), 16), g = parseInt(s.slice(2, 4), 16), b = parseInt(s.slice(4, 6), 16);
    return (tdiv(r * 31 + 127, 255) << 11) | (tdiv(g * 63 + 127, 255) << 5) | tdiv(b * 31 + 127, 255);
  };
  const rgb8 = (c) => [tdiv((c >> 11) * 255, 31), tdiv(((c >> 5) & 63) * 255, 63), tdiv((c & 31) * 255, 31)];
  const pack = (r, g, b) => (tdiv(r * 31 + 127, 255) << 11) | (tdiv(g * 63 + 127, 255) << 5) | tdiv(b * 31 + 127, 255);

  function accentOf(c565, light, maxL) {                       // th_accent_of()
    if (!light) return c565;
    let [r, g, b] = rgb8(c565);
    const l = tdiv(2126 * r + 7152 * g + 722 * b, 10000);
    if (l > maxL) { r = tdiv(r * maxL, l); g = tdiv(g * maxL, l); b = tdiv(b * maxL, l); return pack(r, g, b); }
    return c565;
  }
  function gradTop(accent, luma) {                             // ui_grad_set()
    let [r, g, b] = rgb8(accent);
    const l = tdiv(2126 * r + 7152 * g + 722 * b, 10000) || 1;
    r = tdiv(r * luma + tdiv(l, 2), l); g = tdiv(g * luma + tdiv(l, 2), l); b = tdiv(b * luma + tdiv(l, 2), l);
    r = tdiv(r + luma + 1, 2); g = tdiv(g + luma + 1, 2); b = tdiv(b + luma + 1, 2);
    return pack(Math.min(255, r), Math.min(255, g), Math.min(255, b));
  }
  function makeGradAt(top, bottom, fbH) {                      // ui_grad_at(): dithered, both directions
    const thr = [1, 5, 3, 7];
    const tl = [(top >> 11) & 31, (top >> 5) & 63, top & 31], bl = [(bottom >> 11) & 31, (bottom >> 5) & 63, bottom & 31];
    return (y) => {
      if (y >= fbH) y = fbH - 1;
      const den = fbH - 1, rem = den - y, t = thr[y & 3];
      const out = [];
      for (let k = 0; k < 3; k++) {
        if (tl[k] >= bl[k]) { const num = (tl[k] - bl[k]) * rem; let base = tdiv(num, den); if ((num - base * den) * 8 > t * den) base++; out.push(bl[k] + base); }
        else { const num = (bl[k] - tl[k]) * y; let base = tdiv(num, den); if ((num - base * den) * 8 > t * den) base++; out.push(tl[k] + base); }
      }
      return (out[0] << 11) | (out[1] << 5) | out[2];
    };
  }
  /* A resolved theme: role -> RGB565, the effective accent, and gradAt(y) for a screen FB_H rows tall. */
  function makeTheme(data, themeIdx, light, accentIdx, fbH) {
    const t = data.themes[themeIdx], d = light ? t.light : t.dark;
    const role = {};
    for (const k of Object.keys(d)) if (k !== 'bg_luma') role[k] = snap(d[k]);
    const accent = accentOf(data.palette[accentIdx].c565, light, data.lightAccMaxL);
    const top = gradTop(accent, d.bg_luma);
    return { name: t.name, light: !!light, role, accent, gradTop: top, gradAt: makeGradAt(top, role.bg_bottom, fbH || 360) };
  }
  const api = { snap, rgb8, accentOf, gradTop, makeGradAt, makeTheme };
  if (typeof module !== 'undefined') module.exports = api; else root.TauTheme = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
