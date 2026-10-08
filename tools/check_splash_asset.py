#!/usr/bin/env python3
"""Validate the generated Tau loading asset without third-party packages."""

import struct
from pathlib import Path


ASSET = Path(__file__).resolve().parent.parent / "dist/Assets/tau/alfatreze.TAU/tau-loading.bin"


def main():
    data = ASSET.read_bytes()
    if len(data) < 12:
        raise SystemExit("Tau splash asset is shorter than its header")
    magic, width, height, rle_bytes = struct.unpack_from("<4sHHI", data)
    ncol = {b"TAU1": 16, b"TAU2": 256}.get(magic)
    if not ncol:
        raise SystemExit("Tau splash asset has the wrong magic")
    hdr = 12 + 2 * ncol
    if (width, height) != (400, 360):
        raise SystemExit(f"Tau splash must be 400x360, got {width}x{height}")
    if rle_bytes & 1 or len(data) != hdr + rle_bytes:
        raise SystemExit("Tau splash RLE length is invalid")

    pixels = sum(data[hdr::2])
    if pixels != width * height:
        raise SystemExit(f"Tau splash covers {pixels} pixels, expected {width * height}")
    if any(index >= ncol for index in data[hdr + 1::2]):
        raise SystemExit("Tau splash contains an invalid palette index")
    print(f"PASS: Tau splash {magic.decode()} {width}x{height}, {rle_bytes // 2} runs, {len(data)} bytes")


if __name__ == "__main__":
    main()
