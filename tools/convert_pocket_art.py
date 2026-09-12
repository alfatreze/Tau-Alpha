#!/usr/bin/env python3
"""Convert PNG artwork to Analogue Pocket's rotated monochrome .bin format.

Pocket stores one 16-bit word per pixel after rotating the source 90 degrees
counter-clockwise.  The first byte holds inverted grayscale intensity and the
second byte is zero.  Platform art is 521x165; core-author icons are 36x36.
"""

import argparse
from pathlib import Path

from PIL import Image


SIZES = {
    "platform": (521, 165),
    "author": (36, 36),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=sorted(SIZES))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    image = Image.open(args.source).convert("RGBA")
    expected = SIZES[args.kind]
    if image.size != expected:
        raise SystemExit(f"{args.kind} artwork must be {expected[0]}x{expected[1]}, got {image.size[0]}x{image.size[1]}")

    white = Image.new("RGBA", image.size, (255, 255, 255, 255))
    white.alpha_composite(image)
    gray = white.convert("L").transpose(Image.Transpose.ROTATE_90)

    output = bytearray()
    for value in gray.tobytes():
        output.extend((255 - value, 0))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    expected_bytes = expected[0] * expected[1] * 2
    if len(output) != expected_bytes:
        raise SystemExit(f"internal size mismatch: wrote {len(output)}, expected {expected_bytes}")
    print(f"{args.output}: {len(output)} bytes")


if __name__ == "__main__":
    main()
