#!/usr/bin/env python3
"""Capture Tau's *shipped* loading image for visual review.

This decodes ``dist/Assets/tau/common/tau-loading.bin`` exactly as firmware
does: its 16-entry RGB565 palette plus run-length stream become a 400x360
framebuffer image.  Open the capture next to the authored source to expose
changes caused by the palette and codec before a card swap.

It intentionally does not pretend to simulate the Pocket display panel.  A
device photograph is still the authority for OLED gamma, brightness and glare.

    make visual-review
    python3 tools/capture_splash_frame.py --output work/previews/loading.png
"""

import argparse
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
ASSET = ROOT / "dist/Assets/tau/common/tau-loading.bin"
WIDTH, HEIGHT = 400, 360


def rgb565_to_rgb(value):
    """Match RGB565's expanded channel values for a review-friendly PNG."""
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def decode_asset(path):
    data = path.read_bytes()
    if len(data) < 44:
        raise SystemExit("splash asset is shorter than its header")
    magic, width, height, rle_bytes = struct.unpack_from("<4sHHI", data)
    if magic != b"TAU1" or (width, height) != (WIDTH, HEIGHT):
        raise SystemExit("splash asset header is not a 400x360 TAU1 image")
    if rle_bytes & 1 or len(data) != 44 + rle_bytes:
        raise SystemExit("splash asset RLE length is invalid")

    palette = [rgb565_to_rgb(v) for v in struct.unpack_from("<16H", data, 12)]
    pixels = []
    for offset in range(44, len(data), 2):
        count, index = data[offset], data[offset + 1]
        if not count or index >= len(palette) or len(pixels) + count > WIDTH * HEIGHT:
            raise SystemExit("splash asset contains an invalid RLE run")
        pixels.extend([palette[index]] * count)
    if len(pixels) != WIDTH * HEIGHT:
        raise SystemExit("splash asset does not fill its framebuffer")

    return pixels, palette, rle_bytes // 2


def write_bmp(path, pixels):
    """Write an uncompressed 24-bit BMP using only the Python standard library."""
    row_bytes = (WIDTH * 3 + 3) & ~3
    image_bytes = row_bytes * HEIGHT
    header = struct.pack("<2sIHHI", b"BM", 54 + image_bytes, 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, WIDTH, HEIGHT, 1, 24, 0,
                          image_bytes, 2835, 2835, 0, 0)
    raw = bytearray()
    # BMP is bottom-up BGR.  The padding calculation keeps this correct if the
    # Pocket framebuffer width changes later.
    pad = bytes(row_bytes - WIDTH * 3)
    for y in range(HEIGHT - 1, -1, -1):
        for r, g, b in pixels[y * WIDTH:(y + 1) * WIDTH]:
            raw.extend((b, g, r))
        raw.extend(pad)
    path.write_bytes(header + raw)


def write_capture(path, pixels):
    """Use macOS's built-in converter for PNG, with BMP as a portable fallback."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix.lower() == ".bmp":
        write_bmp(path, pixels)
        return path
    if not shutil.which("sips"):
        raise SystemExit("PNG output needs macOS 'sips'; use a .bmp output path instead")
    with tempfile.TemporaryDirectory(prefix="tau-splash-") as temp:
        bmp = Path(temp) / "framebuffer.bmp"
        write_bmp(bmp, pixels)
        subprocess.run(["sips", "-s", "format", "png", str(bmp), "--out", str(path)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset", type=Path, default=ASSET)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "work/previews/tau-loading-framebuffer.png")
    args = parser.parse_args()

    pixels, palette, run_count = decode_asset(args.asset)
    output = write_capture(args.output, pixels)
    print(f"wrote {output}")
    print(f"decoded {run_count} RLE runs using {len(set(palette))} palette entries")


if __name__ == "__main__":
    main()
