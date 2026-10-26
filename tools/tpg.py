#!/usr/bin/env python3
"""TPG: Tau pixel grid, a screenshot-native container for a Tau report (docs/features/BARCODE_STUDY.md).

The Pocket's screenshot is a lossless 400x360 PNG whose pixels are exact RGB565 values (hardware-confirmed 2026-10-04: four
dense test patterns and a real Check report came back 100% exact). TPG puts the report's binary record straight into those
pixels instead of spending the area on a camera-grade barcode.

Layout TPG2 (current): the stream fills a SQUARE block centred on the screen, as small as the report allows, so a screenshot shows
a compact picture in the middle (the firmware adds a frame and captions around it) and not a strip of pixels on the first row.
The block's side comes from a fixed ladder; the decoder tries each step and keeps the one whose first pixels read `TPG2`.

Stream (identical in both modes): 16 header bytes, then the payload, zero padded to the unit size. All integers big endian.

    0   4  "TPG2"
    4   1  mode: 0 = L (lossless), 1 = R (robust)
    5   1  flags, 0
    6   2  reserved, 0
    8   4  payload length in bytes
   12   4  CRC32 (zlib) of the payload

Mode L: one pixel per 16 stream bits, the pixel's RGB565 value is the two stream bytes (high byte first), row-major inside a
        S x S pixel block, S in LADDER_L (64..360), block at x0 = (400 - S) / 2, y0 = (360 - S) / 2. Up to 259,184 bytes.
        Survives only a lossless copy of the screenshot.
Mode R: one 4x4 pixel cell per 6 stream bits (MSB first: R, G, B, two bits each), row-major inside a C x C cell block, C in
        LADDER_R (20..90 cells = 80..360 px), block centred the same way. The four levels per channel are the RGB565 values
        0/10/21/31 (R, B) and 0/21/42/63 (G). Up to 6,059 bytes. Survives JPEG (quality >= 80 tested) and a resize of the
        whole image by box, Lanczos, nearest or bicubic filters: the decoder resamples to 400x360 and averages the centre of
        each cell. A heavy bilinear downscale corrupts about 14% of the cells at 50% and is detected by the CRC, not repaired:
        there is no error correction. It does not survive a camera.

Layout TPG1 (legacy, the first hardware test 2026-10-04): the stream starts at pixel (0, 0) and runs row-major over the whole
screen (mode L: 1 px per 16 bits; mode R: a 100 x 90 cell grid), magic `TPG1`. Still decoded; no longer produced.

The payload CRC32 makes a bad channel fail loudly. The payload is the Check/Sweep/Info record (`fw/suite_core.h`, parsed by
`decode_tau_suite.py`).

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
GW, GH = W // CW, H // CH          # legacy TPG1 robust grid: 100 x 90 cells
MAGIC = b"TPG2"
MAGIC_V1 = b"TPG1"
MODE_L, MODE_R = 0, 1
HDR = 16
LV5 = (0, 10, 21, 31)              # robust levels as RGB565 field values
LV6 = (0, 21, 42, 63)
# Block sides. Every value is a multiple of 4 pixels (mode R: of 8), so a block is centred on whole, even pixel coordinates (the
# firmware writes pixel pairs) and robust cells stay aligned with the 4-pixel grid.
LADDER_L = (64, 80, 96, 112, 128, 160, 192, 224, 256, 288, 320, 360)   # pixels (never smaller than 64: a visible square)
LADDER_R = (20, 24, 28, 32, 40, 48, 56, 64, 72, 80, 90)                                                 # cells (80..360 px)


def header(mode: int, payload: bytes, magic: bytes = MAGIC) -> bytes:
    return magic + bytes([mode, 0, 0, 0]) + struct.pack(">II", len(payload), zlib.crc32(payload) & 0xFFFFFFFF)


def _units(side: int, mode: int) -> int:
    """Stream bytes a block of `side` (pixels for L, cells for R) can hold, header included."""
    return side * side * 2 if mode == MODE_L else side * side * 6 // 8


def capacity(mode: int) -> int:
    return _units((LADDER_L if mode == MODE_L else LADDER_R)[-1], mode) - HDR


def block(length: int, mode: int):
    """(x0, y0, pixel side) of the centred square block a payload of `length` bytes uses, or None if it does not fit."""
    for s in (LADDER_L if mode == MODE_L else LADDER_R):
        if _units(s, mode) >= HDR + length:
            px = s if mode == MODE_L else s * CW
            return (W - px) // 2, (H - px) // 2, px
    return None


def encode(payload: bytes, mode: int = MODE_L) -> np.ndarray:
    """Returns an (H, W) uint16 array of RGB565 values; everything outside the block is 0 (callers draw their own page)."""
    blk = block(len(payload), mode)
    if blk is None:
        raise ValueError(f"payload of {len(payload)} bytes exceeds mode {'LR'[mode]} capacity {capacity(mode)}")
    x0, y0, px = blk
    data = header(mode, payload) + payload
    img = np.zeros((H, W), np.uint16)
    if mode == MODE_L:
        data += b"\0" * (len(data) % 2)
        p = np.frombuffer(data, ">u2").astype(np.uint16)
        sq = np.zeros(px * px, np.uint16)
        sq[:len(p)] = p
        img[y0:y0 + px, x0:x0 + px] = sq.reshape(px, px)
        return img
    side = px // CW
    bits = np.unpackbits(np.frombuffer(data, np.uint8))
    bits = np.concatenate([bits, np.zeros((-len(bits)) % 6, np.uint8)]).reshape(-1, 6)
    r = bits[:, 0] * 2 + bits[:, 1]
    g = bits[:, 2] * 2 + bits[:, 3]
    b = bits[:, 4] * 2 + bits[:, 5]
    cells = (np.array(LV5, np.uint16)[r] << 11) | (np.array(LV6, np.uint16)[g] << 5) | np.array(LV5, np.uint16)[b]
    grid = np.zeros(side * side, np.uint16)
    grid[:len(cells)] = cells
    img[y0:y0 + px, x0:x0 + px] = np.repeat(np.repeat(grid.reshape(side, side), CH, 0), CW, 1)
    return img


def to_rgb(img565: np.ndarray) -> np.ndarray:
    """What the Pocket screenshot contains: RGB565 expanded to 8 bits per channel by bit replication."""
    r = (img565 >> 11) & 31
    g = (img565 >> 5) & 63
    b = img565 & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)


def from_rgb(rgb: np.ndarray) -> np.ndarray:
    a = rgb.astype(np.uint16)
    return ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)


def _parse(stream: bytes, mode: int, magic: bytes, cap: int):
    if stream[:4] != magic or stream[4] != mode:
        return None
    n, crc = struct.unpack(">II", stream[8:16])
    if n > cap:
        raise ValueError("TPG header length is impossible (damaged)")
    return n, crc


def _check(raw: bytes, n: int, crc: int, what: str) -> bytes:
    if len(raw) != n or zlib.crc32(raw) & 0xFFFFFFFF != crc:
        raise ValueError(f"{what}: CRC mismatch (the screenshot was altered or cropped, or too much damage)")
    return raw


def _cell_levels(img: np.ndarray, x0: int, y0: int, side: int) -> np.ndarray:
    """(side*side, 3) levels 0..3 of a robust block: mean of the 2x2 centre of each 4x4 cell (edges carry the neighbours' blur)."""
    blk = img[y0:y0 + side * CH, x0:x0 + side * CW].astype(np.float32).reshape(side, CH, side, CW, 3)[:, 1:3, :, 1:3].mean((1, 3))
    return np.clip(np.rint(blk / 85.0), 0, 3).astype(np.uint8).reshape(-1, 3)


def _levels_to_bytes(lv: np.ndarray) -> bytes:
    bits = np.stack([lv[:, 0] >> 1, lv[:, 0] & 1, lv[:, 1] >> 1, lv[:, 1] & 1, lv[:, 2] >> 1, lv[:, 2] & 1], 1).reshape(-1)
    return np.packbits(bits[:len(bits) // 8 * 8]).tobytes()


def decode(rgb: np.ndarray) -> bytes:
    """rgb: (h, w, 3) uint8 screenshot pixels. Returns the payload or raises ValueError."""
    exact = rgb.shape[:2] == (H, W)
    if exact:
        flat565 = from_rgb(rgb)
        for s in LADDER_L:                                          # mode L, TPG2: header in the first pixels of the centred block
            x0, y0 = (W - s) // 2, (H - s) // 2
            head = flat565[y0, x0:x0 + HDR // 2].astype(">u2").tobytes()
            got = _parse(head, MODE_L, MAGIC, capacity(MODE_L))
            if got:
                n, crc = got
                sq = flat565[y0:y0 + s, x0:x0 + s].reshape(-1)
                return _check(sq[:(HDR + n + 1) // 2].astype(">u2").tobytes()[HDR:HDR + n], n, crc, "TPG2 mode L")
        head = flat565.reshape(-1)[:HDR // 2].astype(">u2").tobytes()          # legacy TPG1 mode L: from pixel (0, 0)
        got = _parse(head, MODE_L, MAGIC_V1, W * H * 2 - HDR)
        if got:
            n, crc = got
            return _check(flat565.reshape(-1)[:(HDR + n + 1) // 2].astype(">u2").tobytes()[HDR:HDR + n], n, crc, "TPG1 mode L")
    img = rgb
    if not exact:                                                   # resized copy: back to the Pocket's size
        img = np.asarray(Image.fromarray(rgb).resize((W, H), Image.BOX if rgb.shape[1] > W else Image.BILINEAR))
    for s in LADDER_R:                                              # mode R, TPG2: probe the header (22 cells) of each ladder step
        px = s * CW
        x0, y0 = (W - px) // 2, (H - px) // 2
        head = _levels_to_bytes(_cell_levels(img, x0, y0, s)[:(HDR * 8 + 5) // 6])[:HDR]
        got = _parse(head, MODE_R, MAGIC, capacity(MODE_R))
        if got:
            n, crc = got
            return _check(_levels_to_bytes(_cell_levels(img, x0, y0, s))[HDR:HDR + n], n, crc, "TPG2 mode R")
    f = _levels_to_bytes(np.clip(np.rint(img.astype(np.float32).reshape(GH, CH, GW, CW, 3)[:, 1:3, :, 1:3].mean((1, 3)) / 85.0), 0, 3)
                         .astype(np.uint8).reshape(-1, 3))           # legacy TPG1 mode R: 100 x 90 cells from (0, 0)
    got = _parse(f, MODE_R, MAGIC_V1, GW * GH * 6 // 8 - HDR)
    if got:
        n, crc = got
        return _check(f[HDR:HDR + n], n, crc, "TPG1 mode R")
    raise ValueError("no TPG grid found (not a Tau pixel grid, or the image is a camera photo)")


def decode_file(path: str) -> bytes:
    return decode(np.asarray(Image.open(path).convert("RGB")))


def encode_v1(payload: bytes, mode: int = MODE_L) -> np.ndarray:
    """The legacy TPG1 layout (stream from pixel (0, 0)); only used by the tests to prove old captures still decode."""
    data = header(mode, payload, MAGIC_V1) + payload
    img = np.zeros((H, W), np.uint16)
    if mode == MODE_L:
        data += b"\0" * (len(data) % 2)
        p = np.frombuffer(data, ">u2").astype(np.uint16)
        img.reshape(-1)[:len(p)] = p
        return img
    bits = np.unpackbits(np.frombuffer(data, np.uint8))
    bits = np.concatenate([bits, np.zeros((-len(bits)) % 6, np.uint8)]).reshape(-1, 6)
    cells = (np.array(LV5, np.uint16)[bits[:, 0] * 2 + bits[:, 1]] << 11) | (np.array(LV6, np.uint16)[bits[:, 2] * 2 + bits[:, 3]] << 5) \
        | np.array(LV5, np.uint16)[bits[:, 4] * 2 + bits[:, 5]]
    grid = np.zeros(GW * GH, np.uint16)
    grid[:len(cells)] = cells
    return np.repeat(np.repeat(grid.reshape(GH, GW), CH, 0), CW, 1)


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
