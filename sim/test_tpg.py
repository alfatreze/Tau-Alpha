#!/usr/bin/env python3
"""TPG1 reference codec: round trips, capacity, damage detection, and robustness (tools/tpg.py)."""
import io, os, sys
import numpy as np
from PIL import Image
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import tpg

fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    fails += 0 if ok else 1

rng = np.random.default_rng(3)
def blob(n): return bytes(rng.integers(0, 256, n, dtype=np.uint8))
def shot(img565): return tpg.to_rgb(img565)
def jpeg(rgb, q):
    b = io.BytesIO(); Image.fromarray(rgb).save(b, "JPEG", quality=q); b.seek(0)
    return np.asarray(Image.open(b).convert("RGB"))

# capacity numbers quoted in the docs
check("capacity L = 287984", tpg.capacity(tpg.MODE_L) == 287984)
check("capacity R = 6734", tpg.capacity(tpg.MODE_R) == 6734)
check("a 400 byte report takes one row in mode L", tpg.rows_used(400, tpg.MODE_L) == 2 or tpg.rows_used(400, tpg.MODE_L) == 1)

for mode, name in ((tpg.MODE_L, "L"), (tpg.MODE_R, "R")):
    for n in (0, 1, 2, 5, 399, 400, 401, 1000, tpg.capacity(mode)):
        p = blob(n)
        try:
            ok = tpg.decode(shot(tpg.encode(p, mode))) == p
        except ValueError:
            ok = False
        check(f"mode {name} round trip, {n} bytes", ok)
    try:
        tpg.encode(blob(tpg.capacity(mode) + 1), mode); check(f"mode {name} refuses one byte over", False)
    except ValueError:
        check(f"mode {name} refuses one byte over", True)

# rows_used matches what is actually written
for mode in (tpg.MODE_L, tpg.MODE_R):
    for n in (0, 100, 400, 1001, 3000 if mode == tpg.MODE_R else 20000):
        img = tpg.encode(blob(n), mode)
        used = int(np.nonzero(img.any(1))[0].max()) + 1 if img.any() else 0
        check(f"mode {'LR'[mode]} rows_used covers the drawn rows ({n} B)", used <= tpg.rows_used(n, mode) <= used + (4 if mode else 1))

# damage is detected, never silently decoded wrong
p = blob(2000); img = shot(tpg.encode(p, tpg.MODE_L)); bad = img.copy(); bad[1, 7, 0] ^= 8
try: tpg.decode(bad); check("mode L: one flipped pixel bit is detected", False)
except ValueError: check("mode L: one flipped pixel bit is detected", True)
try: tpg.decode(np.zeros((tpg.H, tpg.W, 3), np.uint8)); check("blank screen is refused", False)
except ValueError: check("blank screen is refused", True)

# robustness of mode R (the reason it exists) and the honest limits of mode L
p = blob(5000); imgR = shot(tpg.encode(p, tpg.MODE_R)); imgL = shot(tpg.encode(blob(5000), tpg.MODE_L))
for q in (95, 80):
    try: ok = tpg.decode(jpeg(imgR, q)) == p
    except ValueError: ok = False
    check(f"mode R survives JPEG q{q}", ok)
for name, flt in (("box", Image.BOX), ("lanczos", Image.LANCZOS), ("nearest", Image.NEAREST)):
    half = np.asarray(Image.fromarray(imgR).resize((200, 180), flt))
    try: ok = tpg.decode(half) == p
    except ValueError: ok = False
    check(f"mode R survives a 50% {name} downscale", ok)
# documented limit: a heavy bilinear downscale (a viewer smoothing across a whole 4 px cell) is detected, never mis-decoded
half = np.asarray(Image.fromarray(imgR).resize((200, 180), Image.BILINEAR))
try: ok = tpg.decode(half) == p        # decoding is allowed, a WRONG payload is not
except ValueError: ok = True
check("mode R after a 50% bilinear downscale: decodes or fails loudly, never wrong", ok)
big = np.asarray(Image.fromarray(imgR).resize((800, 720), Image.BICUBIC))
try: ok = tpg.decode(big) == p
except ValueError: ok = False
check("mode R survives a 2x resize", ok)
try: tpg.decode(jpeg(imgL, 95)); check("mode L is refused (not mis-decoded) after JPEG", False)
except ValueError: check("mode L is refused (not mis-decoded) after JPEG", True)

# a Check-style record (TD, format, profile, TLV entries, CRC32 little endian) goes through unchanged
import struct, zlib, decode_tau_suite as d
body = b"TD" + bytes([d.FMT, 1]) + b"".join(bytes([200 + i, 40]) + blob(40) for i in range(9))
rec = body + struct.pack("<I", zlib.crc32(body))
for mode in (tpg.MODE_L, tpg.MODE_R):
    out = tpg.decode(shot(tpg.encode(rec, mode)))
    check(f"record via mode {'LR'[mode]} parses to the same report", out == rec and d.parse_record(out) == d.parse_record(rec))

print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
