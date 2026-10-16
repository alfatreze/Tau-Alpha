#!/usr/bin/env python3
"""The cover palette probe (fw2/art/art.c) against a draw engine whose palette blit lands some time after it is queued (sim/p2_art_probe_harness.c). It must give the right answer for every
landing time up to its 50 ms poll, for both mailbox half orders and both palette skews, and must FAIL (not guess) when the copy never lands. The mutant reads the destination once at a fixed
time (the old behaviour: 'ART ERR 8' on every track on the Pocket)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def build(td, mutant):
    d = Path(td)
    for sub in ("art", "platform", "gfx", "stream", "cores", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/art/art.c").read_text()
    if mutant:
        a = src.index("    const uint32_t t0 = cycles();\n    do {"); b = src.index("    art_probe_raw = r;")
        src = src[:a] + "    { const uint32_t t = cycles(); while ((uint32_t)(cycles() - t) < 512u) { } if (gfx_mb_read(dst, &r)) return TIMG_E_MAILBOX; }\n" + src[b:]
    (d / "art/art.c").write_text(src)
    for f in ("art/art.h", "platform/mmio.h", "platform/platform.h", "gfx/gfx.h", "stream/slot.h", "cores/timg_core.h", "cores/stream_core.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    h = (ROOT / "sim/p2_art_probe_harness.c").read_text().replace('"../fw2/art/art.c"', '"../art/art.c"').replace('"../fw2/platform/platform.h"', '"../platform/platform.h"')
    (d / "sim/h.c").write_text(h)
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(exe), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, land, half, skew):
    o = subprocess.run([str(exe), str(land), str(half), str(skew)], capture_output=True, text=True).stdout.split()
    return int(o[1]), int(o[5])
with tempfile.TemporaryDirectory() as td:
    good = build(td, False)
    want = lambda skew: 255 if skew else 0
    ok = all(run(good, land, half, skew) == (0, want(skew)) for land in (0, 400, 20000, 400000, 2000000) for half in (0, 1) for skew in (0, 1))
    check("right answer for every landing time up to 30 ms, both half orders, both palette skews", ok)
    check("a copy that never lands (200 ms) is an error, not a guess", run(good, 20000000, 0, 0)[0] == 8)
with tempfile.TemporaryDirectory() as td:
    bad = build(td, True)
    check("mutant (one read at a fixed time) is caught: it fails when the copy lands late", run(bad, 400000, 0, 0)[0] == 8)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
