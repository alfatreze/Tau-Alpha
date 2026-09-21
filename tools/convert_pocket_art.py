#!/usr/bin/env python3
"""Convert PNG artwork to Analogue Pocket's rotated monochrome .bin format.

Pocket stores one 16-bit word per pixel after rotating the source 90 degrees
counter-clockwise.  The first byte holds inverted grayscale intensity and the
second byte is zero.  Platform art is 521x165; core-author icons are 36x36.
"""

import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError:          # no Pillow: use the built-in PNG reader below (8-bit, non-interlaced)
    Image = None


SIZES = {
    "platform": (521, 165),
    "author": (36, 36),
}


def read_png_rgba(path):
    """Minimal PNG reader for the fallback: 8-bit gray/RGB/RGBA/gray+alpha, not interlaced. -> (w, h, rgba bytes)."""
    import struct
    import zlib
    d = Path(path).read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG file")
    p, idat, hdr = 8, b"", None
    while p < len(d):
        ln, typ = struct.unpack(">I4s", d[p:p + 8])
        body = d[p + 8:p + 8 + ln]
        if typ == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat += body
        p += 12 + ln
    w, h, depth, ctype, _, _, interlace = hdr
    if depth != 8 or interlace or ctype not in (0, 2, 4, 6):
        raise SystemExit("fallback PNG reader needs an 8-bit non-interlaced gray/RGB/RGBA PNG (or install Pillow)")
    bpp = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    q = 0
    for _ in range(h):
        f = raw[q]
        cur = bytearray(raw[q + 1:q + 1 + stride])
        q += 1 + stride
        for i in range(stride):
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                cur[i] = (cur[i] + a) & 255
            elif f == 2:
                cur[i] = (cur[i] + b) & 255
            elif f == 3:
                cur[i] = (cur[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 255
        rows.append(cur)
        prev = cur
    out = bytearray()
    for r in rows:
        for x in range(w):
            px = r[x * bpp:(x + 1) * bpp]
            if ctype == 0:
                out += bytes((px[0], px[0], px[0], 255))
            elif ctype == 2:
                out += bytes((px[0], px[1], px[2], 255))
            elif ctype == 4:
                out += bytes((px[0], px[0], px[0], px[1]))
            else:
                out += bytes(px)
    return w, h, bytes(out)


def gray_rotated_no_pillow(path):
    """Same result as: composite on white, convert('L'), ROTATE_90. Returns (w, h, bytes of the rotated gray image)."""
    w, h, rgba = read_png_rgba(path)
    gray = bytearray(w * h)
    for i in range(w * h):
        r, g, b, a = rgba[4 * i:4 * i + 4]
        # PIL alpha_composite on opaque white, then convert("L") (ITU-R 601 in 16-bit fixed point)
        def comp(c):
            return (c * a + 255 * (255 - a) + 127) // 255
        rr, gg, bb = comp(r), comp(g), comp(b)
        gray[i] = (rr * 19595 + gg * 38470 + bb * 7471 + 0x8000) >> 16
    # ROTATE_90 = 90 degrees counter-clockwise: out(x', y') = in(w - 1 - y', x'), out is h wide and w tall
    out = bytearray(w * h)
    for yo in range(w):
        for xo in range(h):
            out[yo * h + xo] = gray[xo * w + (w - 1 - yo)]
    return h, w, bytes(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=sorted(SIZES))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    expected = SIZES[args.kind]
    if Image is None:
        w0, h0, _ = read_png_rgba(args.source)
        if (w0, h0) != expected:
            raise SystemExit(f"{args.kind} artwork must be {expected[0]}x{expected[1]}, got {w0}x{h0}")
        _, _, gray_bytes = gray_rotated_no_pillow(args.source)
    else:
        image = Image.open(args.source).convert("RGBA")
        if image.size != expected:
            raise SystemExit(f"{args.kind} artwork must be {expected[0]}x{expected[1]}, got {image.size[0]}x{image.size[1]}")
        white = Image.new("RGBA", image.size, (255, 255, 255, 255))
        white.alpha_composite(image)
        gray_bytes = white.convert("L").transpose(Image.Transpose.ROTATE_90).tobytes()

    output = bytearray()
    for value in gray_bytes:
        output.extend((255 - value, 0))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    expected_bytes = expected[0] * expected[1] * 2
    if len(output) != expected_bytes:
        raise SystemExit(f"internal size mismatch: wrote {len(output)}, expected {expected_bytes}")
    print(f"{args.output}: {len(output)} bytes")


if __name__ == "__main__":
    main()
