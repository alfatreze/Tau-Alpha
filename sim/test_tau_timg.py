#!/usr/bin/env python3
"""Host test for fw/timg.inc + fw/timg_core.h (B-285): real .timg files from tools/tau_image.py (palette-256, several sizes and
aspect ratios incl. odd widths and 128 x 128) are read by the firmware code under a hardware emulation (tag-buffer slot read,
SDRAM mailbox with both half orders, draw engine BLIT/CBLIT + CLUT) and the pixels that reach the art stash are compared with the
Python decoder. Also: corrupt/absent files are refused with the right code and draw nothing, a broken mailbox disables the
reader, and the cover path / reuse key derive from the library track path. Skipped (not failed) when Pillow/numpy are missing."""
import struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
try:
    import numpy as np
    from PIL import Image
    import tau_image as T
except Exception as e:                                   # pragma: no cover
    print("SKIP (needs Pillow and numpy):", e); sys.exit(0)

fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else f"  {detail}"))
    if not ok: fails += 1

ART_IMG = 128
def make(w, h, seed):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:h, 0:w]
    a = np.stack([(x * 255 // max(1, w - 1)), (y * 255 // max(1, h - 1)), (rng.integers(0, 255, (h, w)) // 4 + x // 2) % 256], -1).astype(np.uint8)
    return a

def expected(data):
    """what the stash's art panel holds: the palette image centred in the ART_IMG x ART_IMG cover area (untouched = 0); nothing is cropped now that the cover area is 128."""
    fmt, bpp, w, h, nc, pl = struct.unpack("<xxxxBBHHHI", data[:16])
    clut = np.frombuffer(data[16:16 + 512], "<u2")
    idx = np.frombuffer(data[16 + 512:16 + 512 + w * h], np.uint8).reshape(h, w)
    img = clut[idx]
    cw, ch = min(w, ART_IMG), min(h, ART_IMG)
    sx, sy = (w - cw) // 2, (h - ch) // 2
    out = np.zeros((ART_IMG, ART_IMG), np.uint16)
    dx, dy = (ART_IMG - cw) // 2, (ART_IMG - ch) // 2
    out[dy:dy + ch, dx:dx + cw] = img[sy:sy + ch, sx:sx + cw]
    return out

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    exe = td / "h"
    r = subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                        "-o", str(exe), str(ROOT / "sim/timg_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)

    def run(path, swap=0, mb=1):
        out = td / "out.bin"
        if out.exists(): out.unlink()
        p = subprocess.run([str(exe), str(path), str(out), str(swap), str(mb)], capture_output=True, text=True)
        return p.returncode, p.stdout, (np.fromfile(out, "<u2").reshape(ART_IMG, ART_IMG) if out.exists() else None)

    for (w, h) in [(128, 128), (85, 128), (128, 83), (91, 91), (40, 30), (1, 5)]:
        rgb = make(w, h, w * 1000 + h)
        data = T.encode(rgb, "pal256")
        f = td / f"c_{w}x{h}.timg"; f.write_bytes(data)
        for swap in (0, 1):
            rc, out, px = run(f, swap)
            good = rc == 0 and px is not None and np.array_equal(px, expected(data))
            check(f"{w}x{h} swap={swap}: pixels equal the decoder's palette (centred, uncropped)", good, out.strip().replace("\n", " | ") if not good else "")
    # a 128 wide image needs two CBLITs (127-word limit): seen in the harness output
    rc, out, px = run(td / "c_128x128.timg")
    check("128 px wide covers are drawn in pieces of at most 120 words", "maxw=" in out and int(out.split("maxw=")[1].split()[0]) <= 120, out)
    check("cover path and reuse key derive from the library path", "path=/Assets/tau_test/common/Album One/tau-art/cover_128.pal256.timg" in out and "sig=0" not in out, out)

    good = (td / "c_128x128.timg").read_bytes()
    def variant(name, mutate, want_rc):
        b = bytearray(good); mutate(b)
        f = td / (name + ".timg"); f.write_bytes(bytes(b))
        rc, out, px = run(f)
        check(f"{name}: refused with code {want_rc}, nothing drawn", rc == want_rc and px is None, out)
    variant("bad magic", lambda b: b.__setitem__(0, ord("X")), 1)
    variant("rgb565 format", lambda b: b.__setitem__(4, 1), 2)
    variant("4-bit palette", lambda b: b.__setitem__(5, 4), 2)
    variant("width 129", lambda b: b.__setitem__(slice(6, 8), struct.pack("<H", 129)), 3)
    variant("payload length wrong", lambda b: b.__setitem__(slice(12, 16), struct.pack("<I", 1000)), 3)
    trunc = td / "trunc.timg"; trunc.write_bytes(good[:-100])
    rc, out, px = run(trunc); check("truncated file: read failure, nothing drawn", rc == 5 and px is None, out)
    rc, out, px = run(td / "missing.timg"); check("missing file: open failure, nothing drawn", rc == 4 and px is None, out)
    rc, out, px = run(td / "c_85x128.timg", 0, 0); check("broken mailbox: reader unavailable, nothing drawn", rc == 6 and px is None, out)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
