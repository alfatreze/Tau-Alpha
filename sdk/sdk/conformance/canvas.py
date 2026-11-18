"""Replays the host-table calls of a tick on a model of the player's draw engine, to SEE the picture and to check where it lands. The semantics follow the meter lab's framebuffer
(tools/meters/preview/tau_fb.js of the Tau Alpha repository): a 400 x 360 RGB565 frame plus the off-screen stash in the same engine memory (pitch 512 pixels, 1024 rows), one command per call.
The lab has no font, so text is drawn as 5 x 6 blocks, 7 pixels per character (the firmware's width is about that)."""
from array import array

W, H, PITCH, ROWS = 400, 360, 512, 1024
TEXT_ADV, TEXT_H = 7, 16


def rgb(c): return (((c >> 11) * 255 + 15) // 31, (((c >> 5) & 63) * 255 + 31) // 63, ((c & 31) * 255 + 15) // 31)


class Canvas:
    def __init__(self):
        self.px = array("H", [0]) * (W * H); self.st = array("H", [0]) * (PITCH * ROWS); self.clut = [0] * 256; self.ba = None

    def get(self, y, x):
        return self.px[y * W + x] if y < H and x < W else self.st[y * PITCH + x] if 0 <= y < ROWS and 0 <= x < PITCH else 0

    def set(self, y, x, v):
        if y < H and x < W:
            if y >= 0 and x >= 0: self.px[y * W + x] = v
        elif 0 <= y < ROWS and 0 <= x < PITCH: self.st[y * PITCH + x] = v

    def fill(self, x, y, w, h, c):
        x0, x1, y0, y1 = max(0, x), min(W, x + w), max(0, y), min(H, y + h)
        for yy in range(y0, y1):
            self.px[yy * W + x0:yy * W + x1] = array("H", [c]) * max(0, x1 - x0)

    def apply(self, call):
        """call = (name, args...) as the runner prints it (decimal strings; text is hex)"""
        n = call[0]; a = call[1:]
        if n == "rect": self.fill(*(int(v) for v in a[:5]))
        elif n == "bar":
            x, y, w, h, lit, fg, bg = (int(v) for v in a)
            self.fill(x, y + h - lit, w, lit, fg)
            if h > lit: self.fill(x, y, w, h - lit, bg)
        elif n == "rect_stash":
            x, y, w, h, c = (int(v) for v in a)
            for yy in range(y, min(ROWS, y + h)):
                for xx in range(x, min(PITCH, x + w)): self.set(yy, xx, c)
        elif n == "text":
            x, y, hexs, fg, bg = int(a[0]), int(a[1]), a[2].rstrip("."), int(a[3]), int(a[4]); s = bytes.fromhex(hexs).decode("latin1")
            for i, ch in enumerate(s):
                if ch != " ": self.fill(x + TEXT_ADV * i, y + 2, 5, 6, fg)
        elif n in ("blit", "blit_abs"):
            sx_, sy, dx, dy, w, h = (int(v) for v in a)
            rows = [[self.get(sy + j, sx_ + i) for i in range(w)] for j in range(h)]
            for j in range(h):
                for i in range(w): self.set(dy + j, dx + i, rows[j][i])
        elif n == "scroll_down":
            x, y, w, h, d = (int(v) for v in a)
            if 0 < d < h:
                rows = [self.px[(y + j) * W + x:(y + j) * W + x + w] for j in range(h - d)]
                for j in range(h - d): self.px[(y + j + d) * W + x:(y + j + d) * W + x + w] = rows[j]
        elif n == "scroll_left":
            x, y, w, h, d = (int(v) for v in a)
            if 0 < d < w:
                rows = [self.px[(y + j) * W + x + d:(y + j) * W + x + w] for j in range(h)]
                for j in range(h): self.px[(y + j) * W + x:(y + j) * W + x + w - d] = rows[j]
        elif n == "sblit":
            sx_, sy, dx, dy, w, h, sc = (int(v) for v in a); ow, oh = min(127, w * sc), h * sc
            for j in range(oh):
                for i in range(ow): self.set(dy + j, dx + i, self.get(sy + j // sc, sx_ + i // sc))
        elif n == "cblit":
            sx_, sy, dx, dy, w, h = (int(v) for v in a)
            for j in range(h):
                for i in range(w):
                    X, Y = dx + i, dy + j
                    if 0 <= X < W and 0 <= Y < H: self.px[Y * W + X] = self.clut[self.get(sy + j, sx_ + i) & 255]
        elif n == "blend_on": self.ba = int(a[0])
        elif n == "blend_off": self.ba = None
        elif n == "blend_blit":
            sx_, sy, dx, dy, w, h = (int(v) for v in a); al = 255 if self.ba is None else self.ba
            ch = lambda b, f: (f * al + b * (256 - al)) >> 8
            for j in range(h):
                for i in range(w):
                    X, Y = dx + i, dy + j
                    if not (0 <= X < W and 0 <= Y < H): continue
                    f = self.get(sy + j, sx_ + i); b = self.px[Y * W + X]
                    self.px[Y * W + X] = (ch(b >> 11, f >> 11) << 11) | (ch((b >> 5) & 63, (f >> 5) & 63) << 5) | ch(b & 31, f & 31)
        elif n == "mb_write":
            ad, d = int(a[0]), int(a[1]); self.set(ad >> 9, ad & 511, d & 0xFFFF); self.set((ad + 1) >> 9, (ad + 1) & 511, (d >> 16) & 0xFFFF)

    def apply_palette(self, start_vals):
        """palette load: ['start n c0 c1 ...'] given as the runner's 'c clut_load start colors n' line plus its 'p' line"""
        pass

    def crop_rgb(self, x, y, w, h):
        out = bytearray()
        for yy in range(y, y + h):
            for xx in range(x, x + w): out += bytes(rgb(self.px[yy * W + xx]))
        return bytes(out)


def bounds_violations(call, box, profile_strict=True):
    """-> list of plain-words problems for one call against the rectangle (x, y, w, h) the meter owns"""
    bx, by, bw, bh = box; n = call[0]; a = call[1:]; v = []
    def inside(x, y, w, h): return w == 0 or h == 0 or (x >= bx and y >= by and x + w <= bx + bw and y + h <= by + bh)
    def offscreen(x, y, w, h): return x >= W or y >= H or x + w <= 0 or y + h <= 0
    def vis_outside(x, y, w, h):
        # a write into engine memory (stash or mailbox) lands on screen when it touches the visible frame outside the meter's box
        x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
        if x1 <= x0 or y1 <= y0: return False
        return not (x0 >= bx and y0 >= by and x1 <= bx + bw and y1 <= by + bh)
    if n in ("rect", "bar"):
        x, y, w, h = (int(t) for t in a[:4])
        if not inside(x, y, w, h) and not offscreen(x, y, w, h): v.append("%s(%d, %d, %d, %d) is outside the meter's box (%d, %d, %d, %d)" % (n, x, y, w, h, bx, by, bw, bh))
    elif n == "text":
        x, y = int(a[0]), int(a[1]); s = bytes.fromhex(a[2].rstrip(".")); wd = TEXT_ADV * len(s)
        if not inside(x, y, wd, TEXT_H): v.append("text at (%d, %d), %d characters, runs outside the meter's box (%d, %d, %d, %d)" % (x, y, len(s), bx, by, bw, bh))
    elif n in ("rect_stash",):
        x, y, w, h = (int(t) for t in a[:4])
        if vis_outside(x, y, w, h): v.append("rect_stash(%d, %d, %d, %d) writes into the visible screen outside the meter's box" % (x, y, w, h))
    elif n in ("blit", "blend_blit", "cblit"):
        dx, dy, w, h = int(a[2]), int(a[3]), int(a[4]), int(a[5])
        if not inside(dx, dy, w, h): v.append("%s to (%d, %d) size %d x %d lands outside the meter's box (%d, %d, %d, %d)" % (n, dx, dy, w, h, bx, by, bw, bh))
    elif n == "sblit":
        dx, dy, w, h, sc = int(a[2]), int(a[3]), int(a[4]), int(a[5]), int(a[6])
        if not inside(dx, dy, min(127, w * sc), h * sc): v.append("sblit to (%d, %d) size %d x %d (x%d) lands outside the meter's box" % (dx, dy, min(127, w * sc), h * sc, sc))
    elif n == "blit_abs":
        dx, dy, w, h = int(a[2]), int(a[3]), int(a[4]), int(a[5])
        if vis_outside(dx, dy, w, h): v.append("blit_abs to (%d, %d) size %d x %d lands on the visible screen outside the meter's box" % (dx, dy, w, h))
    elif n in ("scroll_down", "scroll_left"):
        x, y, w, h = (int(t) for t in a[:4])
        if not inside(x, y, w, h): v.append("%s(%d, %d, %d, %d) moves pixels outside the meter's box" % (n, x, y, w, h))
    elif n == "mb_write":
        ad = int(a[0]); y, x = ad >> 9, ad & 511
        if vis_outside(x, y, 2, 1): v.append("mb_write(0x%x) writes a pixel of the visible screen outside the meter's box" % ad)
    return v
