#!/usr/bin/env python3
"""Convert the Figma meter previews (assets/ui/meter/*.jpg or .png) into fw/meter_thumbs.h.

The firmware draws each 56x32 preview from an 8-colour RGB565 palette (the exports are greyscale) and
a raster run-length stream (one byte per run: top 3 bits = palette index, low 5 bits = run length - 1,
so 1..32 pixels; runs may cross row ends). This keeps all eleven previews to a few KiB of ROM,
where raw RGB565 would need 39 KiB.

Input: any size (Figma exports are usually large). Images are decoded with macOS `sips` (JPEG or
PNG to BMP) or, if installed, Pillow, then area-averaged down to 56x32. The palette is built by a
deterministic k-means (fixed seed), so the header is reproducible.

Usage: python3 tools/gen_meter_thumbs.py [--src assets/ui/meter] [--out fw/meter_thumbs.h] [--preview PNG]
"""
import argparse, os, random, struct, subprocess, sys, tempfile, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
W, H, K = 56, 32, 8
# File stem -> VIZ_* enum name (same order as fw/player.c and fw/settingsui.inc set_viz[]).
ORDER = [("meter-bars", "VIZ_BARS"), ("meter-waterfall", "VIZ_WATER"), ("meter-lr-levels", "VIZ_LEVELS"),
         ("meter-phasescope", "VIZ_SCOPE"), ("meter-oscilloscope", "VIZ_WAVE"), ("meter-vu", "VIZ_VU"),
         ("meter-waveform", "VIZ_SCROLL"), ("meter-mirrored-bars", "VIZ_MIRROR"),
         ("meter-peak-dots", "VIZ_DOTS"), ("meter-magic-eye", "VIZ_EYE"), ("meter-spectrum", "VIZ_LED")]


def decode(path):
    """Return (w, h, rows of (r,g,b))."""
    try:
        from PIL import Image  # optional
        im = Image.open(path).convert("RGB")
        w, h = im.size
        px = list(im.getdata())
        return w, h, [px[y * w:(y + 1) * w] for y in range(h)]
    except ImportError:
        pass
    with tempfile.TemporaryDirectory() as t:
        bmp = os.path.join(t, "x.bmp")
        r = subprocess.run(["sips", "-s", "format", "bmp", str(path), "--out", bmp], capture_output=True)
        if r.returncode:
            sys.exit(f"cannot decode {path}: install Pillow or run on macOS (sips)")
        b = open(bmp, "rb").read()
    off = struct.unpack("<I", b[10:14])[0]
    w, h = struct.unpack("<ii", b[18:26])
    bpp = struct.unpack("<H", b[28:30])[0]
    if bpp not in (24, 32):
        sys.exit(f"unsupported BMP depth {bpp}")
    bytes_px, rs, ah = bpp // 8, ((w * bpp // 8) + 3) // 4 * 4, abs(h)
    rows = []
    for y in range(ah):
        yy = y if h < 0 else ah - 1 - y
        row = b[off + yy * rs: off + yy * rs + w * bytes_px]
        rows.append([(row[x * bytes_px + 2], row[x * bytes_px + 1], row[x * bytes_px]) for x in range(w)])
    return w, ah, rows


def resample(w, h, rows):
    """Area-average to W x H (no-op at 56x32)."""
    if (w, h) == (W, H):
        return rows
    out = []
    for y in range(H):
        y0, y1 = y * h // H, max(y * h // H + 1, (y + 1) * h // H)
        line = []
        for x in range(W):
            x0, x1 = x * w // W, max(x * w // W + 1, (x + 1) * w // W)
            n = (y1 - y0) * (x1 - x0)
            s = [0, 0, 0]
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    p = rows[yy][xx]
                    s[0] += p[0]; s[1] += p[1]; s[2] += p[2]
            line.append((s[0] // n, s[1] // n, s[2] // n))
        out.append(line)
    return out


def d2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def kmeans(pts, k, iters=14):
    rnd = random.Random(7)
    cs = rnd.sample(sorted(set(pts)), k)
    sub = rnd.sample(pts, min(len(pts), 8000))
    for _ in range(iters):
        bk = [[] for _ in cs]
        for p in sub:
            bk[min(range(k), key=lambda i: d2(p, cs[i]))].append(p)
        cs = [tuple(sum(c) // len(b) for c in zip(*b)) if b else cs[i] for i, b in enumerate(bk)]
    return cs


def rgb565(p):
    return ((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3)


def unrgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def runs(idx):
    flat = [i for row in idx for i in row]
    out, i = [], 0
    while i < len(flat):
        n = 1
        while i + n < len(flat) and flat[i + n] == flat[i] and n < 32:
            n += 1
        out.append((flat[i] << 5) | (n - 1))
        i += n
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", type=Path, default=ROOT / "assets/ui/meter")
    ap.add_argument("--out", type=Path, default=ROOT / "fw/meter_thumbs.h")
    ap.add_argument("--preview", type=Path, help="write a contact sheet PNG of the quantised result")
    a = ap.parse_args()
    imgs = []
    for stem, _ in ORDER:
        f = next((a.src / (stem + e) for e in (".jpg", ".jpeg", ".png") if (a.src / (stem + e)).exists()), None)
        if not f:
            sys.exit(f"missing {stem}.jpg/.png in {a.src}")
        w, h, rows = decode(f)
        imgs.append(resample(w, h, rows))
    pals, idxs, cs_all = [], [], []
    for im in imgs:                                  # one palette per preview: keeps its own hues
        pts = [p for r in im for p in r]
        cs = kmeans(pts, K)
        p565 = [rgb565(c) for c in cs]
        cs = [unrgb565(v) for v in p565]             # what the firmware will really draw
        idx = [[min(range(K), key=lambda i: d2(p, cs[i])) for p in r] for r in im]
        freq = [0] * K
        for r in idx:
            for i in r:
                freq[i] += 1
        order = sorted(range(K), key=lambda i: -freq[i])      # most frequent colour first
        remap = {old: new for new, old in enumerate(order)}
        idxs.append([[remap[i] for i in r] for r in idx])
        pals.append([p565[o] for o in order])
        cs_all.append([cs[o] for o in order])
    streams = [runs(idx) for idx in idxs]
    total = sum(len(s) for s in streams)
    lines = ["/* Generated by tools/gen_meter_thumbs.py from assets/ui/meter/*. Do not edit.",
             f" * {len(streams)} previews of {W}x{H}, one {K}-colour RGB565 palette each, raster run-length",
             " * stream: byte = (palette index << 5) | (run length - 1), 1..32 pixels. Indexed by VIZ_* enum value. */",
             f"#define METER_THUMB_W {W}u", f"#define METER_THUMB_H {H}u",
             "static const uint16_t meter_thumb_pal[%d][%d] = {" % (len(pals), K)]
    for pal in pals:
        lines.append("    { " + ", ".join("0x%04Xu" % v for v in pal) + " },")
    lines.append("};")
    offs, pos = [], 0
    for s in streams:
        offs.append(pos); pos += len(s)
    offs.append(pos)
    by_viz = {name: i for i, (_, name) in enumerate(ORDER)}
    lines.append("static const uint16_t meter_thumb_off[%d] = {" % (len(ORDER) + 1))
    lines.append("    /* indexed by VIZ_* enum: */")
    # VIZ enum order in fw/player.c: BARS, WATER, LEVELS, SCOPE, WAVE, VU, SCROLL, MIRROR, DOTS, EYE, LED
    lines.append("    " + ", ".join(str(o) for o in offs))
    lines.append("};")
    lines.append("static const uint8_t meter_thumb_rle[%d] = {" % total)
    flat = [b for s in streams for b in s]
    for i in range(0, len(flat), 24):
        lines.append("    " + ", ".join("0x%02X" % b for b in flat[i:i + 24]) + ",")
    lines.append("};")
    a.out.write_text("\n".join(lines) + "\n")
    print(f"{len(streams)} previews, {total} run bytes + {2 * K * len(pals)} palettes + {2 * (len(ORDER) + 1)} offsets"
          f" = {total + 2 * K * len(pals) + 2 * (len(ORDER) + 1)} bytes -> {a.out}")
    if a.preview:
        S, G, cols = 4, 6, 4
        sw, sh = cols * (W * S + G), 3 * (H * S + G)
        cv = [[(20, 20, 20)] * sw for _ in range(sh)]
        for n, idx in enumerate(idxs):
            ox, oy = (n % cols) * (W * S + G), (n // cols) * (H * S + G)
            for y in range(H):
                for x in range(W):
                    for dy in range(S):
                        for dx in range(S):
                            cv[oy + y * S + dy][ox + x * S + dx] = cs_all[n][idx[y][x]]
        raw = b"".join(b"\x00" + bytes(c for p in r for c in p) for r in cv)
        def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
        a.preview.write_bytes(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", sw, sh, 8, 2, 0, 0, 0))
                              + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))


if __name__ == "__main__":
    main()
