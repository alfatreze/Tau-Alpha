#!/usr/bin/env python3
"""Compare a Pocket screenshot of the PIXEL GRID TEST page with the pattern the firmware draws (fw/pixgrid.h).

  pixgrid_check.py shot.png [pattern 0-3]     pattern is guessed (best match) when omitted

Prints, per pattern, the fraction of pixels that match exactly after converting the screenshot back to RGB565, and
the first mismatches. 100.000% for the right pattern means the screenshot path is bit exact for that content.
"""
import sys
import numpy as np
from PIL import Image

W, H = 400, 360


def pg_pixel_grid(pat):
    y, x = np.mgrid[0:H, 0:W].astype(np.uint64)
    idx = y * W + x
    if pat == 0: return (((idx * 0x9E3779B1) & 0xFFFFFFFF) >> 16).astype(np.uint16)
    if pat == 1: return np.where((x ^ y) & 1, 0xFFFF, 0).astype(np.uint16)
    if pat == 2:
        r = x * 31 // (W - 1); g = y * 63 // (H - 1); b = (x + y) * 31 // (W + H - 2)
        return ((r << 11) | (g << 5) | b).astype(np.uint16)
    return (idx & 0xFFFF).astype(np.uint16)


def shot_565(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.uint16)
    if a.shape[:2] != (H, W): raise SystemExit(f"screenshot is {a.shape[1]}x{a.shape[0]}, expected {W}x{H}")
    return ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)


def main():
    if len(sys.argv) < 2: raise SystemExit(__doc__)
    s = shot_565(sys.argv[1])
    pats = [int(sys.argv[2])] if len(sys.argv) > 2 else range(4)
    best = None
    for p in pats:
        ref = pg_pixel_grid(p)
        for swap in (0, 1):
            r = ref
            if swap:                                  # adjacent pixels exchanged inside each mailbox word
                r = ref.copy(); r[:, 0::2], r[:, 1::2] = ref[:, 1::2], ref[:, 0::2]
            frac = float((s == r).mean())
            print(f"pattern {p} swap {swap}: {frac * 100:8.3f}% exact")
            if best is None or frac > best[0]: best = (frac, p, swap, r)
    frac, p, swap, r = best
    bad = np.argwhere(s != r)
    print(f"best: pattern {p} swap {swap}, {frac * 100:.3f}% exact, {len(bad)} wrong pixels")
    for y, x in bad[:8]: print(f"  ({x},{y}) screen {int(s[y, x]):04X} expected {int(r[y, x]):04X}")


if __name__ == "__main__":
    main()
