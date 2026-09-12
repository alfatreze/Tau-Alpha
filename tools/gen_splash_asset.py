#!/usr/bin/env python3
"""Generate Tau's compact boot-screen asset from the authored source image.

The Pocket framebuffer is 400x360 RGB565.  Storing that raw would cost 288 KB,
more than the complete firmware budget.  The boot image is quantised to 16
colours and run-length encoded into a deferred APF asset.  Firmware streams it
through its existing 4 KB scratch window and draws it with the framebuffer
rectangle engine, so the image consumes no permanent firmware RAM.

The generated binary is checked in so the normal firmware build does not need
Pillow. Regenerate only when the source artwork changes:

    python3 tools/gen_splash_asset.py \
        assets/ui/tau-loading-source.jpg \
        dist/Assets/tau/common/tau-loading.bin \
        --preview work/previews/tau-loading-device.png
"""

import argparse
import struct
from pathlib import Path

from PIL import Image


SRC_W = 400
SRC_H = 360
COLORS = 16


def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def runs(indices):
    out = []
    value = indices[0]
    count = 0
    for item in indices:
        if item == value and count < 255:
            count += 1
        else:
            out.extend((count, value))
            value = item
            count = 1
    out.extend((count, value))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()

    authored = Image.open(args.source).convert("RGB")
    if authored.size != (SRC_W, SRC_H):
        raise SystemExit(f"source must be {SRC_W}x{SRC_H}, got {authored.size}")

    indexed = authored.quantize(
        colors=COLORS,
        method=Image.Quantize.MEDIANCUT,
        dither=Image.Dither.NONE,
    )
    indices = list(indexed.tobytes())
    encoded = runs(indices)
    if sum(encoded[0::2]) != SRC_W * SRC_H:
        raise SystemExit("RLE does not cover the complete image")

    raw_palette = indexed.getpalette()[:COLORS * 3]
    palette = [rgb565(raw_palette[i:i + 3]) for i in range(0, len(raw_palette), 3)]
    header = struct.pack("<4sHHI", b"TAU1", SRC_W, SRC_H, len(encoded))
    header += struct.pack("<16H", *palette)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + bytes(encoded))

    if args.preview:
        args.preview.parent.mkdir(parents=True, exist_ok=True)
        indexed.convert("RGB").save(args.preview)

    print(
        f"{args.output}: {len(header) + len(encoded)} bytes, "
        f"{len(encoded) // 2} runs, "
        f"{COLORS} RGB565 colours"
    )


if __name__ == "__main__":
    main()
