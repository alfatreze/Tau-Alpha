/* RGB565 framebuffer with Tau's quantisation and the mtr_* primitives, counting engine commands the way the firmware pays for them:
 * one command per rect, per bar (OP_BAR), per text run. `log` optionally records every command for golden comparison. */
(function (root) {
  const to565 = (r, g, b) => (((r * 31 + 127) / 255 | 0) << 11) | (((g * 63 + 127) / 255 | 0) << 5) | ((b * 31 + 127) / 255 | 0);
  const from565 = (c) => [((c >> 11) * 255 / 31) | 0, (((c >> 5) & 63) * 255 / 63) | 0, ((c & 31) * 255 / 31) | 0];
  class Fb {
    constructor(w, h, log) { this.w = w; this.h = h; this.px = new Uint16Array(w * h); this.cmds = 0; this.pix = 0; this.ops = 0; this.log = log ? [] : null; this.barReady = true; this.blendReady = true; }
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
    /* the SCROLL view's engine calls (B-952): blit = a copy inside the box being drawn into; scrollLeft = the box moves left by d columns, the right d columns are left as they were (the caller paints them). One command each. */
    blit(sx, sy, dx, dy, w, h) {
      this._rec('blit', [sx, sy, dx, dy, w, h]);
      this.pix += 2 * w * h; this.ops += 2 * w * h; const rows = [];
      for (let yy = 0; yy < h; yy++) rows.push(this.px.slice((sy + yy) * this.w + sx, (sy + yy) * this.w + sx + w));
      for (let yy = 0; yy < h; yy++) this.px.set(rows[yy], (dy + yy) * this.w + dx);
    }
    scrollLeft(x, y, w, h, d) {
      this._rec('scroll_left', [x, y, w, h, d]);
      if (d <= 0 || d >= w) return;
      this.pix += 2 * (w - d) * h; this.ops += 2 * (w - d) * h; const rows = [];
      for (let yy = 0; yy < h; yy++) rows.push(this.px.slice((y + yy) * this.w + x + d, (y + yy) * this.w + x + w));
      for (let yy = 0; yy < h; yy++) this.px.set(rows[yy], (y + yy) * this.w + x);
    }
    /* The off-screen stash and the palette engine (the Waterfall, B-983): the stash is the framebuffer memory below and beside the visible frame (rows 360.., columns up to 511); rect_stash writes raw values there (palette
     * indices for a plane), clut_load fills the palette, cblit shows a stash rectangle through the palette in the drawing target, scroll_down moves a box down (the top d rows stay). One command each, except clut_load (a few register puts). */
    _st() { return this.stash || (this.stash = new Uint16Array(512 * 1024)); }
    _get(y, x) { return y < this.h && x < this.w ? this.px[y * this.w + x] : this._st()[y * 512 + x]; }          // the engine's memory: the visible frame, and the stash below and beside it (pitch 512)
    _set(y, x, v) { if (y < this.h && x < this.w) this.px[y * this.w + x] = v; else this._st()[y * 512 + x] = v; }
    rectStash(x, y, w, h, c) {
      this._rec('rect_stash', [x, y, w, h, c]);
      for (let yy = y; yy < Math.min(1024, y + h); yy++) for (let xx = x; xx < Math.min(512, x + w); xx++) this._set(yy, xx, c);
    }
    clutLoad(start, colors) {
      if (this.log) this.log.push('clut_load ' + [start, colors.length].concat(Array.from(colors)).join(' '));
      this.clut = this.clut || new Uint16Array(256); for (let i = 0; i < colors.length; i++) this.clut[(start + i) & 255] = colors[i];
    }
    cblit(sx, sy, dx, dy, w, h) {
      this._rec('cblit', [sx, sy, dx, dy, w, h]); this.clut = this.clut || new Uint16Array(256);
      this.pix += 2 * w * h; this.ops += 2 * w * h;
      for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) { const X = dx + xx, Y = dy + yy; if (X >= 0 && X < this.w && Y >= 0 && Y < this.h) this.px[Y * this.w + X] = this.clut[this._get(sy + yy, sx + xx) & 255]; }
    }
    /* Chladni's engine path (B-983): blit_abs = a copy inside the engine's memory (stash to stash, whole-memory coordinates), the mailbox = two pixels a word at a pixel address (row * 512 + column, the lower address in the low half),
     * fence = every queued command has executed (logged, not counted), sblit = w x h SOURCE pixels from the stash scaled by 1, 2 or 3 into the drawing target (the output is clamped at 127 columns). */
    blitAbs(sx, sy, dx, dy, w, h) {
      this._rec('blit_abs', [sx, sy, dx, dy, w, h]); const rows = [];
      for (let yy = 0; yy < h; yy++) { const r = []; for (let xx = 0; xx < w; xx++) r.push(this._get(sy + yy, sx + xx)); rows.push(r); }
      for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) this._set(dy + yy, dx + xx, rows[yy][xx]);
    }
    mbWrite(addr, d) { if (this.log) this.log.push('mb_write ' + addr + ' ' + (d >>> 0)); this._set(addr >> 9, addr & 511, d & 0xFFFF); this._set((addr + 1) >> 9, (addr + 1) & 511, (d >>> 16) & 0xFFFF); return 0; }
    mbRead(addr) { if (this.log) this.log.push('mb_read ' + addr); return ((this._get((addr + 1) >> 9, (addr + 1) & 511) << 16) | this._get(addr >> 9, addr & 511)) >>> 0; }
    fence() { if (this.log) this.log.push('fence'); }
    sblit(sx, sy, dx, dy, w, h, scale) {
      this._rec('sblit', [sx, sy, dx, dy, w, h, scale]); const ow = Math.min(127, w * scale), oh = h * scale;
      this.pix += 2 * ow * oh; this.ops += 2 * ow * oh;
      for (let yy = 0; yy < oh; yy++) for (let xx = 0; xx < ow; xx++) { const X = dx + xx, Y = dy + yy; if (X >= 0 && X < this.w && Y >= 0 && Y < this.h) this.px[Y * this.w + X] = this._get(sy + Math.trunc(yy / scale), sx + Math.trunc(xx / scale)); }
    }
    /* The pipelined blend of a stash rectangle (the Scope's trail): blend_on(a) sets the source weight (of 256), blend_blit shows the stash rectangle over the target with it, blend_off ends it. */
    blendOn(a) { if (this.log) this.log.push('blend_on ' + a); this._ba = a; }
    blendOff() { if (this.log) this.log.push('blend_off'); this._ba = undefined; }
    blendBlit(sx, sy, dx, dy, w, h) {
      this._rec('blend_blit', [sx, sy, dx, dy, w, h]); const a = this._ba == null ? 255 : this._ba;
      const ch = (b, f) => (f * a + b * (256 - a)) >> 8;
      for (let yy = 0; yy < h; yy++) for (let xx = 0; xx < w; xx++) {
        const X = dx + xx, Y = dy + yy; if (X < 0 || X >= this.w || Y < 0 || Y >= this.h) continue;
        const f = this._get(sy + yy, sx + xx), i = Y * this.w + X, b = this.px[i];
        this.px[i] = (ch(b >> 11, f >> 11) << 11) | (ch((b >> 5) & 63, (f >> 5) & 63) << 5) | ch(b & 31, f & 31); this.pix++; this.ops += 3;
      }
    }
    scrollDown(x, y, w, h, d) {
      this._rec('scroll_down', [x, y, w, h, d]);
      if (d <= 0 || d >= h) return;
      this.pix += 2 * w * (h - d); this.ops += 2 * w * (h - d); const rows = [];
      for (let yy = 0; yy < h - d; yy++) rows.push(this.px.slice((y + yy) * this.w + x, (y + yy) * this.w + x + w));
      for (let yy = 0; yy < h - d; yy++) this.px.set(rows[yy], (y + yy + d) * this.w + x);
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
    /* Lab-only: a text run is ONE command (the firmware's fb_text); the lab has no font, so each glyph is a 5 x 6 block of the foreground colour (the width is the firmware's about 7 px a character). Returns the run's width. */
    text(x, y, s, fg, bg) {
      this._rec('text', [x, y, s, fg, bg]);
      for (let i = 0; i < s.length; i++) if (s[i] !== ' ') this._fill(x + 7 * i, y + 2, 5, 6, fg);
      return 7 * s.length;
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
