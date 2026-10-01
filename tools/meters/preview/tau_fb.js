/* RGB565 framebuffer with Tau's quantisation and the mtr_* primitives, counting engine commands the way the firmware pays for them:
 * one command per rect, per bar (OP_BAR), per text run. `log` optionally records every command for golden comparison. */
(function (root) {
  const to565 = (r, g, b) => (((r * 31 + 127) / 255 | 0) << 11) | (((g * 63 + 127) / 255 | 0) << 5) | ((b * 31 + 127) / 255 | 0);
  const from565 = (c) => [((c >> 11) * 255 / 31) | 0, (((c >> 5) & 63) * 255 / 63) | 0, ((c & 31) * 255 / 31) | 0];
  class Fb {
    constructor(w, h, log) { this.w = w; this.h = h; this.px = new Uint16Array(w * h); this.cmds = 0; this.pix = 0; this.log = log ? [] : null; this.barReady = true; }
    _rec(op, a) { this.cmds++; if (this.log) this.log.push(op + ' ' + a.join(' ')); }
    rect(x, y, w, h, c) {
      this._rec('rect', [x, y, w, h, c]);
      const x0 = Math.max(0, x), x1 = Math.min(this.w, x + w), y0 = Math.max(0, y), y1 = Math.min(this.h, y + h);
      this.pix += Math.max(0, x1 - x0) * Math.max(0, y1 - y0);
      for (let yy = y0; yy < y1; yy++) this.px.fill(c, yy * this.w + x0, yy * this.w + x1);
    }
    /* fb_bar: a column of height h with `lit` rows lit at the bottom. With OP_BAR (BLIT_READY) it is ONE command; without, the
       firmware falls back to two rects. barReady mirrors that probe so the lab can show both. */
    bar(x, y, w, h, lit, litC, unlitC) {
      if (this.barReady) {
        this._rec('bar', [x, y, w, h, lit, litC, unlitC]);
        const c = this.cmds; this.cmds--;               // draw without double counting
        this._fill(x, y + h - lit, w, lit, litC); if (h > lit) this._fill(x, y, w, h - lit, unlitC);
        this.cmds = c;
      } else {
        this.rect(x, y + h - lit, w, lit, litC); if (h > lit) this.rect(x, y, w, h - lit, unlitC);
      }
    }
    /* fb_blit-style copy inside the framebuffer: ONE command, rows of at most 127 pixels (Talos row buffer). Rows are read before they are written. */
    copy(sx, sy, dx, dy, w, h) {
      this._rec('copy', [sx, sy, dx, dy, w, h]);
      this.pix += 2 * w * h; const rows = [];
      for (let yy = 0; yy < h; yy++) rows.push(this.px.slice((sy + yy) * this.w + sx, (sy + yy) * this.w + sx + w));
      for (let yy = 0; yy < h; yy++) this.px.set(rows[yy], (dy + yy) * this.w + dx);
    }
    _fill(x, y, w, h, c) {
      const x0 = Math.max(0, x), x1 = Math.min(this.w, x + w), y0 = Math.max(0, y), y1 = Math.min(this.h, y + h);
      for (let yy = y0; yy < y1; yy++) this.px.fill(c, yy * this.w + x0, yy * this.w + x1);
    }
    rgba() { const o = new Uint8ClampedArray(this.w * this.h * 4); for (let i = 0; i < this.px.length; i++) { const [r, g, b] = from565(this.px[i]); o[i * 4] = r; o[i * 4 + 1] = g; o[i * 4 + 2] = b; o[i * 4 + 3] = 255; } return o; }
    checksum() { let h = 2166136261 >>> 0; for (let i = 0; i < this.px.length; i++) { h ^= this.px[i]; h = Math.imul(h, 16777619) >>> 0; } return h; }
  }
  const api = { Fb, to565, from565 };
  if (typeof module !== 'undefined') module.exports = api; else root.TauFb = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
