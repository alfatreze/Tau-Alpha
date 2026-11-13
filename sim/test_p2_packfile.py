#!/usr/bin/env python3
"""fw2/stream/packfile.c (external meters P2.4): ONE slot read per 4 KB (the tag zone), not per 1 KB. The real packfile_rd runs over a stub slot driver that counts reads; the biggest shipped pack (Layered Wave) is loaded the way
mpk_install does (header, header, last byte, the image in 4 KB pieces). The count is about four times lower than with the old 1 KB chunk (compiled in as the mutant), the quarter-second timeout is kept, a failed read ends the call at once,
and the bytes are those of the file."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb, build_packs as bp
fails = 0
def check(n, ok, i=""):
    global fails
    print(("ok   " if ok else "FAIL ") + n + ("  " + str(i) if not ok else "")); fails += 0 if ok else 1
def build(td, chunk=None):
    exe = Path(td) / ("h%s" % chunk)
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w"] + (["-DPACKFILE_CHUNK=%du" % chunk] if chunk else []) + ["-o", str(exe), str(ROOT / "sim/p2_packfile_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, s): return subprocess.run([str(exe)], input=s, capture_output=True, text=True).stdout.strip().splitlines()
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td); (tmp / "wk").mkdir()
    ents = bp.build_entries(tmp / "wk"); (tmp / "b.bin").write_bytes(mb.compose(ents))
    rows = mb.open_toc((tmp / "b.bin").read_bytes())["rows"]; big = max(rows, key=lambda r: r["pack_size"])
    size = big["pack_size"] - 48; off = big["pack_off"]
    new = run(build(td), "file %s\nload %d %d\n" % (tmp / "b.bin", off, size))[0].split(); old = run(build(td, 1024), "file %s\nload %d %d\n" % (tmp / "b.bin", off, size))[0].split()
    rn, ro = int(new[4]), int(old[4])
    check("the biggest pack (%d B image) loads in %d slot reads, was %d: %.1f times fewer" % (size, rn, ro, ro / rn), new[2] == "1" and rn * 3 < ro and rn == 3 + -(-size // 4096), (new, old))
    check("no slot read is bigger than the 4 KB tag zone, and the timeout is a quarter second (CLK_HZ / 4)", int(new[6]) == 4096 and int(new[8]) == 66666667 // 4, new)
    o = run(build(td), "file %s\nbig\n" % (tmp / "b.bin"))[0].split()
    check("a 20000-byte read is 5 slot reads of at most 4096", o[4] == "5" and o[6] == "4096", o)
    o = run(build(td), "file %s\nfailat 3\nload %d %d\n" % (tmp / "b.bin", off, size))[0].split()
    check("a failed slot read ends the load AT it: exactly 3 reads, no further read, not ok", o[2] == "0" and o[4] == "3" and o[10] == "1", o)
    check("mutant (1 KB chunks) is caught by the count", ro * 3 > 3 * rn and ro >= 3 * rn)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
