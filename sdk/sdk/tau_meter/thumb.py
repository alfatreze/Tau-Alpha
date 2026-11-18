"""The 56 x 32 preview of a meter (thumb.json): an 8-colour palette and a run-length stream, `(palette index << 5) | (run - 1)` row by row (docs/DESCRIPTOR.md). The METERS list shows it before the meter loads.

  from_rgb(rgb, w, h)   any image -> thumb.json dict (area average down to 56 x 32, then at most 8 colours by popularity of the nearest quantisation)
The palette entries are fixed RGB565 colours; roles can be set by hand in `palette_roles` (a theme role name, or a ramp) so the preview follows the theme."""
RW, RH = 56, 32


def rgb565(r, g, b): return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
def unpack565(v): return ((v >> 8) & 0xF8) | ((v >> 13) & 7), ((v >> 3) & 0xFC) | ((v >> 9) & 3), ((v << 3) & 0xF8) | ((v >> 2) & 7)


def downscale(rgb, w, h):
    out = []
    for y in range(RH):
        y0, y1 = y * h // RH, max((y + 1) * h // RH, y * h // RH + 1)
        for x in range(RW):
            x0, x1 = x * w // RW, max((x + 1) * w // RW, x * w // RW + 1)
            n = (y1 - y0) * (x1 - x0); r = g = b = 0
            for yy in range(y0, y1):
                o = (yy * w + x0) * 3
                for xx in range(x1 - x0): r += rgb[o + 3 * xx]; g += rgb[o + 3 * xx + 1]; b += rgb[o + 3 * xx + 2]
            out.append((r // n, g // n, b // n))
    return out


def quantize(pix, k=8):
    """a simple popularity palette over 5-bit colour buckets, merged greedily until k remain"""
    cnt = {}
    for p in pix:
        key = (p[0] >> 3, p[1] >> 3, p[2] >> 3); cnt[key] = cnt.get(key, 0) + 1
    cols = sorted(cnt.items(), key=lambda kv: -kv[1])
    pal = []
    for key, _ in cols:
        c = (key[0] << 3 | 4, key[1] << 3 | 4, key[2] << 3 | 4)
        if all(sum((a - b) ** 2 for a, b in zip(c, q)) > 48 ** 2 for q in pal): pal.append(c)
        if len(pal) == k: break
    return pal or [(0, 0, 0)]


def nearest(pal, p): return min(range(len(pal)), key=lambda i: sum((a - b) ** 2 for a, b in zip(p, pal[i])))


def rle(indices):
    out = bytearray(); i = 0
    while i < len(indices):
        j = i
        while j < len(indices) and indices[j] == indices[i] and j - i < 32 and (j % RW != 0 or j == i): j += 1
        out.append((indices[i] << 5) | (j - i - 1)); i = j
    return bytes(out)


def from_indices(indices, palette565, roles=None):
    pal = list(palette565) + [0] * (8 - len(palette565))
    d = {"w": RW, "h": RH, "palette": pal[:8], "rle": rle(indices).hex()}
    if roles: d["palette_roles"] = roles
    return d


def from_rgb(rgb, w, h):
    pix = downscale(rgb, w, h); pal = quantize(pix)
    return from_indices([nearest(pal, p) for p in pix], [rgb565(*c) for c in pal])


def decode(thumb):
    """-> list of 56*32 palette indices (and checks the stream)"""
    out = []
    for b in bytes.fromhex(thumb["rle"]): out += [b >> 5] * ((b & 31) + 1)
    if len(out) != RW * RH: raise ValueError("the preview stream holds %d pixels, not %d" % (len(out), RW * RH))
    return out
