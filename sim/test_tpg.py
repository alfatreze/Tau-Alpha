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
check("capacity L = 259184", tpg.capacity(tpg.MODE_L) == 259184)
check("capacity R = 6059", tpg.capacity(tpg.MODE_R) == 6059)
check("a 231 byte report is an 80 px robust square and a 64 px lossless square, both centred", tpg.block(231, 1) == (160, 140, 80) and tpg.block(231, 0) == (168, 148, 64))

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

# the block is a centred square, as small as the report allows, and everything lies inside it
for mode in (tpg.MODE_L, tpg.MODE_R):
    for n in (0, 100, 231, 400, 1001, 3000, 5000 if mode else 20000, tpg.capacity(mode)):
        x0, y0, px = tpg.block(n, mode)
        img = tpg.encode(blob(n) if n else b"", mode)
        ys, xs = np.nonzero(img)
        inside = len(ys) == 0 or (ys.min() >= y0 and ys.max() < y0 + px and xs.min() >= x0 and xs.max() < x0 + px)
        centred = x0 * 2 + px == tpg.W and y0 * 2 + px == tpg.H and x0 % 2 == 0
        ladder = tpg.LADDER_L if mode == tpg.MODE_L else tpg.LADDER_R
        smallest = all(tpg._units(s, mode) < tpg.HDR + n for s in ladder if (s if mode == tpg.MODE_L else s * 4) < px)
        check(f"mode {'LR'[mode]} {n} B: centred square {px}px, inside, smallest that fits", inside and centred and smallest)

# the first hardware captures (TPG1, stream from pixel (0, 0)) still decode
old = blob(231)
for mode in (tpg.MODE_L, tpg.MODE_R):
    try: ok = tpg.decode(shot(tpg.encode_v1(old, mode))) == old
    except ValueError: ok = False
    check(f"legacy TPG1 mode {'LR'[mode]} still decodes", ok)

# damage is detected, never silently decoded wrong
p = blob(2000); img = shot(tpg.encode(p, tpg.MODE_L)); bad = img.copy(); bad[tpg.block(2000, 0)[1] + 1, tpg.block(2000, 0)[0] + 7, 0] ^= 8
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

# the two reports the grid exists for: the Info page's full text (34 rows) and a 64-track Decode Sweep
rows = [(i, f"ROW{i}LABEL", f"VALUE {i * 37 % 1000} B") for i in range(34)]
info = d.build_record(0, [(26, bytes([i]) + l.encode() + b"\0" + v.encode()) for i, l, v in rows])
sweep = d.build_record(1, [(14, bytes([i, 100, 3, 0, 11, 0, 55, 0, 0, 0]) + b"Track title %02d" % i) for i in range(64)])
for name, rec in (("Info page (34 rows)", info), ("64-track sweep", sweep)):
    for mode in (tpg.MODE_L, tpg.MODE_R):
        try: out = tpg.decode(shot(tpg.encode(rec, mode)))
        except ValueError: out = None
        check(f"{name}: {len(rec)} B survives mode {'LR'[mode]}", out == rec)
rep = d.parse_record(tpg.decode(shot(tpg.encode(info, tpg.MODE_L))))
tab = rep["entries"]["infotext"]
check("Info rows decode with label and value", len(tab) == 34 and tab[5] == {"row": 5, "label": "ROW5LABEL", "value": "VALUE 185 B"})
check("the Info record is above what a version-14 QR code holds (the reason for the grid)", len(info) > 330)

# the always-on context: Info identity rows + now playing
ctx = d.build_record(1, [(26, bytes([1]) + b"FPGA REV\0" + b"4D50331A"), (27, bytes([3, 4, 0, 11, 0]) + "Aqua Marina\0Anna Måne\0Album One".encode())])
rep = d.parse_record(tpg.decode(shot(tpg.encode(ctx, tpg.MODE_R))))
np_ = rep["entries"]["nowplaying"]
check("now playing decodes", np_ == {"state": "playing", "queue_pos": 4, "queue_len": 11, "title": "Aqua Marina", "artist": "Anna Måne", "album": "Album One"})
check("context row decodes", rep["entries"]["infotext"] == [{"row": 1, "label": "FPGA REV", "value": "4D50331A"}])
nothing = d.parse_record(d.build_record(1, [(27, bytes([0, 0, 0, 0, 0]) + b"\0\0")]))
check("nothing loaded decodes", nothing["entries"]["nowplaying"]["state"] == "nothing loaded" and nothing["entries"]["nowplaying"]["title"] == "")

print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
