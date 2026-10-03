#!/usr/bin/env python3
"""B-567: regenerates the small JPEG fixtures for sim/test_art_decode.py (committed; only rerun on purpose,
because the golden hashes in sim/fixtures/art/golden.json belong to these exact bytes).
Deterministic content (gradients, a few shapes, seeded noise); baseline JPEGs in the shapes fw/art.inc handles:
the full-decode path (small images), the reduce path (large images), grayscale, 4:4:4, odd sizes, and one
progressive file (picojpeg refuses it: PJPG_UNSUPPORTED_MODE)."""
import random
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent / "fixtures" / "art"


def picture(w, h, seed):
    rng = random.Random(seed)
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255 // max(w - 1, 1)), (y * 255 // max(h - 1, 1)), ((x + y) * 255 // max(w + h - 2, 1)))
    d = ImageDraw.Draw(img)
    for _ in range(12):
        x0, y0 = rng.randrange(w), rng.randrange(h)
        d.ellipse([x0, y0, x0 + rng.randrange(8, max(9, w // 3)), y0 + rng.randrange(8, max(9, h // 3))],
                  fill=(rng.randrange(256), rng.randrange(256), rng.randrange(256)))
    return img


CASES = [  # name, w, h, mode, subsampling, progressive, quality
    ("small_420_300", 300, 300, "RGB", 2, False, 85),      # below 736 px: FULL decode path
    ("small_444_200x120", 200, 120, "RGB", 0, False, 85),  # 4:4:4, non-square
    ("odd_457x331", 457, 331, "RGB", 2, False, 80),        # not a multiple of 8 or 16
    ("large_1000", 1000, 1000, "RGB", 2, False, 60),       # REDUCE path
    ("gray_160", 160, 160, "L", None, False, 90),          # mono (one component)
    ("tiny_64", 64, 64, "RGB", 2, False, 90),              # smaller than the 128 px panel (magnified out)
    ("progressive_300", 300, 300, "RGB", 2, True, 85),     # unsupported: must fail the same way every time
]

if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for i, (name, w, h, mode, sub, prog, q) in enumerate(CASES):
        img = picture(w, h, 1000 + i)
        if mode == "L":
            img = img.convert("L")
        kw = dict(quality=q, progressive=prog, optimize=False)
        if sub is not None:
            kw["subsampling"] = sub
        img.save(OUT / f"{name}.jpg", "JPEG", **kw)
        print(name, (OUT / f"{name}.jpg").stat().st_size)
