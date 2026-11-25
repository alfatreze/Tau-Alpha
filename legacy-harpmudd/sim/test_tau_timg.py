#!/usr/bin/env python3
"""Host test for fw/timg.inc + fw/timg_core.h (B-285): real .timg files from tools/tau_image.py (palette-256, several sizes and
aspect ratios incl. odd widths and 128 x 128) are read by the firmware code under a hardware emulation (tag-buffer slot read,
SDRAM mailbox with both half orders, draw engine BLIT/CBLIT + CLUT) and the pixels that reach the art stash are compared with the
Python decoder. Also: corrupt/absent files are refused with the right code and draw nothing, a broken mailbox disables the
reader, and the cover path / reuse key derive from the library track path. Skipped (not failed) when Pillow/numpy are missing."""
import os, struct, subprocess, sys, tempfile
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

# B-575: CLUT_FIXED=1 models a bitstream with the registered CLUT write address (start index 0, no slot skew); default = the legacy RTL.
CLUT_FLAGS = ["-DCLUT_SKEW=0", "-DCLUT_START_IDX=0u"] if os.environ.get("CLUT_FIXED") else []
print("CLUT model:", "registered write address (start 0)" if CLUT_FLAGS else "legacy write skew (start 255)")
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    exe = td / "h"
    r = subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable", *CLUT_FLAGS,
                        "-o", str(exe), str(ROOT / "sim/timg_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)

    def run(path, swap=0, mb=1, overlay=0, delay=0, retry=0, repeat=0, exe=exe):
        out = td / "out.bin"
        if out.exists(): out.unlink()
        p = subprocess.run([str(exe), str(path), str(out), str(swap), str(mb), str(overlay), str(delay), str(retry), str(repeat)], capture_output=True, text=True)
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
    rc, out, px = run(td / "c_85x128.timg", 0, 0); check("broken mailbox: probe refuses (code 8), nothing drawn", rc == 8 and px is None, out)
    # B-325: the reader never loaded a cover on a Pocket. Two causes are reproduced here and must stay fixed.
    ref = expected((td / "c_128x128.timg").read_bytes())
    rc, out, px = run(td / "c_128x128.timg", 0, 1, overlay=1)
    check("a menu is up (FB_HELD): the cover still loads, into the off-screen stash", rc == 0 and px is not None and np.array_equal(px, ref), out)
    rc, out, px = run(td / "c_128x128.timg", 1, 1, overlay=1, delay=30)
    check("the draw engine is behind a burst of drawing (copy lands after 30 reads) and a menu is up: still loads", rc == 0 and px is not None and np.array_equal(px, ref), out)
    rc, out, px = run(td / "c_128x128.timg", 0, 1, delay=20000)
    check("an engine that never delivers within the probe window is refused with code 8 (not remembered as a bad file)", rc == 8 and px is None and "miss=0" in out, out)
    rc, out, px = run(td / "c_128x128.timg", 0, 0, retry=1)
    check("a failed probe is not remembered: the next attempt loads", "retry shown=1" in out and "miss=0" in out.split("retry")[0] and px is not None and np.array_equal(px, ref), out)
    rc, out, px = run(td / "c_128x128.timg", 0, 0, repeat=6)
    check("repeated probe failures stop probing after 3 (six attempts: fail counter 7, probe failures capped at 3)", "repeat fails=7 probe_fails=3" in out, out)
    rc, out, px = run(td / "missing.timg", 0, 1)
    check("a missing FILE is remembered (miss=1) so the album's other tracks do not retry", "miss=1" in out, out)
    # mutation checks: the tests above must FAIL against the old behaviour, or they prove nothing
    src = (ROOT / "fw/timg.inc").read_text()
    def mutant(name, fn, **kw):
        m = fn(src)
        assert m != src, name
        (td / "timg_mut.inc").write_text(m)
        h = (ROOT / "sim/timg_harness.c").read_text().replace('#include "../fw/timg.inc"', f'#include "{td}/timg_mut.inc"').replace('#include "../fw/timg_core.h"', f'#include "{ROOT}/fw/timg_core.h"')
        (td / "hm.c").write_text(h)
        r = subprocess.run(["cc", "-std=c11", "-O1", "-I", str(ROOT / "fw"), *CLUT_FLAGS, "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-o", str(td / "hm"), str(td / "hm.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return run(td / "c_128x128.timg", exe=td / "hm", **kw)
    rc, out, px = mutant("no exemption", lambda t: t.replace("const uint8_t ov_saved = ov_draw; ov_draw = 1u;", "const uint8_t ov_saved = ov_draw;"), overlay=1)
    check("mutant without the FB_HELD exemption is caught (cover not loaded while a menu is up)", not (rc == 0 and px is not None and np.array_equal(px, ref)), out)
    rc, out, px = mutant("single read", lambda t: t.replace("} while ((uint32_t)(cycles() - t0) < CLK_HZ / 20u);", "} while (0);"), delay=30)
    check("mutant that reads the probe once is caught (engine behind a burst)", not (rc == 0 and px is not None and np.array_equal(px, ref)), out)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
