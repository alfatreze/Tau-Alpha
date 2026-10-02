/* RGB565 framebuffer with Tau's quantisation and the mtr_* primitives, counting engine commands the way the firmware pays for them:
 * one command per rect, per bar (OP_BAR), per text run. `log` optionally records every command for golden comparison. */
(function (root) {
  const to565 = (r, g, b) => (((r * 31 + 127) / 255 | 0) << 11) | (((g * 63 + 127) / 255 | 0) << 5) | ((b * 31 + 127) / 255 | 0);
  const from565 = (c) => [((c >> 11) * 255 / 31) | 0, (((c >> 5) & 63) * 255 / 63) | 0, ((c & 31) * 255 / 31) | 0];
  class Fb {
    constructor(w, h, log) { this.w = w; this.h = h; this.px = new Uint16Array(w * h); this.cmds = 0; this.pix = 0; this.ops = 0; this.log = log ? [] : null; this.barReady = true; }
    _rec(op, a) { this.cmds++; if (this.log) this.log.push(op + ' ' + a.join(' ')); }
    rect(x, y, w, h, c) {
      this._rec('rect', [x, y, w, h, c]);
      const x0 = Math.max(0, x), x1 = Math.min(this.w, x + w), y0 = Math.max(0, y), y1 = Math.min(this.h, y + h);
      this.pix += Math.max(0, x1 - x0) * Math.max(0, y1 - y0); this.ops += Math.max(0, x1 - x0) * Math.max(0, y1 - y0);   // ops = SDRAM word operations: a fill writes once
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
      this.pix += 2 * w * h; this.ops += 2 * w * h; const rows = [];
      for (let yy = 0; yy < h; yy++) rows.push(this.px.slice((sy + yy) * this.w + sx, (sy + yy) * this.w + sx + w));
      for (let yy = 0; yy < h; yy++) this.px.set(rows[yy], (dy + yy) * this.w + dx);
    }
    /* Lab-only: a rect drawn through the hardware blend (B5), F = c over the pixel already there B; mode 0..4 as in mp3_fb.sv (0 DSP alpha, 1 average,
       2 add, 3 subtract, 4 B+F/4). Counted as ONE command: on hardware it is an OP_BLIT with blend from a one-row strip of the colour (src_stride 0). */
    blend(x, y, w, h, c, mode, alpha) {
      this._rec('blend', [x, y, w, h, c, mode, alpha]);
      const x0 = Math.max(0, x), x1 = Math.min(this.w, x + w), y0 = Math.max(0, y), y1 = Math.min(this.h, y + h);
      const ch = (b, f, mx) => mode === 0 ? ((f * alpha + b * (256 - alpha)) >> 8) : mode === 1 ? ((b + f) >> 1) : mode === 2 ? Math.min(mx, b + f) : mode === 3 ? (f > b ? 0 : b - f) : Math.min(mx, b + (f >> 2));
      const fr = c >> 11, fg = (c >> 5) & 63, fb = c & 31;
      for (let yy = y0; yy < y1; yy++) for (let xx = x0; xx < x1; xx++) {
        const i = yy * this.w + xx, b = this.px[i];
        this.px[i] = (ch(b >> 11, fr, 31) << 11) | (ch((b >> 5) & 63, fg, 63) << 5) | ch(b & 31, fb, 31);
        this.pix++; this.ops += 3;   // a blended pixel reads the destination, reads the source and writes
      }
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
