#!/usr/bin/env python3
"""TPG1: Tau pixel grid, a screenshot-native container for a Tau report (docs/features/BARCODE_STUDY.md).

The Pocket's screenshot is a lossless 400x360 PNG whose pixels are exact RGB565 values (hardware-confirmed 2026-10-04: four
dense test patterns came back 100% exact). TPG1 puts the report's binary record straight into those pixels instead of
spending the area on a camera-grade barcode.

Stream (identical in both modes): 16 header bytes, then the payload, zero padded to the unit size. All integers big endian.

    0   4  "TPG1"
    4   1  mode: 0 = L (lossless), 1 = R (robust)
    5   1  flags, 0
    6   2  reserved, 0
    8   4  payload length in bytes
   12   4  CRC32 (zlib) of the payload

Mode L: one pixel per 16 stream bits, the pixel's RGB565 value is the two stream bytes (high byte first). Pixel p is at
        (x, y) = (p % 400, p // 400), the stream starts at (0, 0). 400x360 holds 288,000 bytes; a 400 byte report is one row.
        Survives only a lossless copy of the screenshot.
Mode R: one 4x4 pixel cell per 6 stream bits (MSB first: R, G, B, two bits each), cell k at (k % 100, k // 100) in a 100x90
        cell grid. The four levels per channel are the RGB565 values 0/10/21/31 (R, B) and 0/21/42/63 (G). 6,750 bytes per
        screen. Survives JPEG (quality >= 80 tested) and a resize of the whole image by box, Lanczos, nearest or bicubic
        filters: the decoder resamples to 400x360 and averages the centre of each cell. A heavy bilinear downscale (a viewer
        smoothing across a whole cell) corrupts about 14% of the cells at 50% and is detected by the CRC, not repaired:
        there is no error correction in TPG1. It does not survive a camera.

The decoder tries mode L (magic in the first 16 pixels), then mode R (magic in the first cells). The payload CRC32 makes a
bad channel fail loudly. The payload is the Check/Sweep/Info record (`fw/suite_core.h`, parsed by `decode_tau_suite.py`).

  tpg.py encode record.bin out.png [--mode L|R]
  tpg.py decode shot.png out.bin
"""
import argparse
import struct
import sys
import zlib

import numpy as np
from PIL import Image

W, H = 400, 360
CW, CH = 4, 4                      # robust cell
GW, GH = W // CW, H // CH          # 100 x 90 cells
MAGIC = b"TPG1"
MODE_L, MODE_R = 0, 1
HDR = 16
LV5 = (0, 10, 21, 31)              # robust levels as RGB565 field values
LV6 = (0, 21, 42, 63)


def header(mode: int, payload: bytes) -> bytes:
    return MAGIC + bytes([mode, 0, 0, 0]) + struct.pack(">II", len(payload), zlib.crc32(payload) & 0xFFFFFFFF)


def capacity(mode: int) -> int:
    return (W * H * 2 if mode == MODE_L else GW * GH * 6 // 8) - HDR


def rows_used(length: int, mode: int) -> int:
    """Pixel rows a stream of `length` payload bytes occupies (0 if it does not fit)."""
    if length > capacity(mode):
        return 0
    n = HDR + length
    if mode == MODE_L:
        return -(-(-(-n // 2)) // W)                       # ceil(ceil(n / 2) / 400)
    return -(-(-(-n * 8 // 6)) // GW) * CH                 # ceil(ceil(n * 8 / 6) / 100) cell rows of 4 pixel rows


def encode(payload: bytes, mode: int = MODE_L) -> np.ndarray:
    """Returns an (H, W) uint16 array of RGB565 values; rows past the stream are 0 (callers draw their own page)."""
    if len(payload) > capacity(mode):
        raise ValueError(f"payload of {len(payload)} bytes exceeds mode {'LR'[mode]} capacity {capacity(mode)}")
    data = header(mode, payload) + payload
    img = np.zeros((H, W), np.uint16)
    if mode == MODE_L:
        data += b"\0" * (len(data) % 2)
        px = np.frombuffer(data, ">u2").astype(np.uint16)
        img.reshape(-1)[:len(px)] = px
        return img
    bits = np.unpackbits(np.frombuffer(data, np.uint8))
    bits = np.concatenate([bits, np.zeros((-len(bits)) % 6, np.uint8)]).reshape(-1, 6)
    r = bits[:, 0] * 2 + bits[:, 1]
    g = bits[:, 2] * 2 + bits[:, 3]
    b = bits[:, 4] * 2 + bits[:, 5]
    cells = (np.array(LV5, np.uint16)[r] << 11) | (np.array(LV6, np.uint16)[g] << 5) | np.array(LV5, np.uint16)[b]
    grid = np.zeros(GW * GH, np.uint16)
    grid[:len(cells)] = cells
    return np.repeat(np.repeat(grid.reshape(GH, GW), CH, 0), CW, 1)


def to_rgb(img565: np.ndarray) -> np.ndarray:
    """What the Pocket screenshot contains: RGB565 expanded to 8 bits per channel by bit replication."""
    r = (img565 >> 11) & 31
    g = (img565 >> 5) & 63
    b = img565 & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)


def from_rgb(rgb: np.ndarray) -> np.ndarray:
    a = rgb.astype(np.uint16)
    return ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)


def _parse(stream: bytes, mode: int):
    if stream[:4] != MAGIC or stream[4] != mode:
        return None
    n, crc = struct.unpack(">II", stream[8:16])
    if n > capacity(mode):
        raise ValueError("TPG header length is impossible (damaged)")
    return n, crc


def decode(rgb: np.ndarray) -> bytes:
    """rgb: (h, w, 3) uint8 screenshot pixels. Returns the payload or raises ValueError."""
    if rgb.shape[:2] == (H, W):
        flat = from_rgb(rgb).reshape(-1)
        head = flat[:HDR // 2].astype(">u2").tobytes()
        got = _parse(head, MODE_L)
        if got:
            n, crc = got
            raw = flat[:(HDR + n + 1) // 2].astype(">u2").tobytes()[HDR:HDR + n]
            if zlib.crc32(raw) & 0xFFFFFFFF != crc:
                raise ValueError("TPG1 mode L: CRC mismatch (the screenshot was altered or cropped)")
            return raw
    img = rgb
    if rgb.shape[:2] != (H, W):                                   # resized copy: back to the Pocket's size, area averaged
        img = np.asarray(Image.fromarray(rgb).resize((W, H), Image.BOX if rgb.shape[1] > W else Image.BILINEAR))
    f = img.astype(np.float32).reshape(GH, CH, GW, CW, 3)[:, 1:3, :, 1:3].mean((1, 3))   # mean of the 2x2 centre of each cell (edges carry the neighbours' blur)
    lv = np.clip(np.rint(f / 85.0), 0, 3).astype(np.uint8).reshape(-1, 3)    # 0, 85, 170, 255 -> 0..3
    bits = np.stack([lv[:, 0] >> 1, lv[:, 0] & 1, lv[:, 1] >> 1, lv[:, 1] & 1, lv[:, 2] >> 1, lv[:, 2] & 1], 1).reshape(-1)
    stream = np.packbits(bits[:len(bits) // 8 * 8]).tobytes()
    got = _parse(stream, MODE_R)
    if not got:
        raise ValueError("no TPG1 grid found (not a Tau pixel grid, or the image is a camera photo)")
    n, crc = got
    raw = stream[HDR:HDR + n]
    if len(raw) != n or zlib.crc32(raw) & 0xFFFFFFFF != crc:
        raise ValueError("TPG1 mode R: CRC mismatch (too much damage)")
    return raw


def decode_file(path: str) -> bytes:
    return decode(np.asarray(Image.open(path).convert("RGB")))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("encode"); e.add_argument("src"); e.add_argument("dst"); e.add_argument("--mode", choices="LR", default="L")
    d = sub.add_parser("decode"); d.add_argument("src"); d.add_argument("dst")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "encode":
            img = to_rgb(encode(open(a.src, "rb").read(), MODE_L if a.mode == "L" else MODE_R))
            Image.fromarray(img).save(a.dst)
        else:
            open(a.dst, "wb").write(decode_file(a.src))
    except ValueError as ex:
        print(f"invalid: {ex}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
