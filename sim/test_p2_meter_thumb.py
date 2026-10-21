#!/usr/bin/env python3
"""The METER list previews (fw2/ui/meterlist_thumb.c): the plane built in the stash equals an independent decode of each meters/<key>/thumb.json, the palette loaded is that meter's own, and the blit takes
the 56x32 plane to the requested position. Mutant: runs that cross a row end are not wrapped."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(n, ok, i=""):
    global fails
    print(("ok   " if ok else "FAIL ") + n + ("  " + i if i and not ok else ""))
    fails += 0 if ok else 1
sys.path.insert(0, str(ROOT / "tools/player2"))
def run(mutant):
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        for sub in ("ui", "gfx", "generated", "sim"): (d / sub).mkdir()
        src = (ROOT / "fw2/ui/meterlist_thumb.c").read_text()
        if mutant:
            a = "const uint32_t n = run < MTH_W - x ? run : MTH_W - x;"; assert a in src; src = src.replace(a, "const uint32_t n = run;")
        (d / "ui/meterlist_thumb.c").write_text(src)
        for f in ("ui/meterlist_thumb.h", "gfx/gfx.h", "generated/meter_registry.h", "generated/meter_thumbs.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
        (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_thumb_harness.c").read_text().replace('"../fw2/ui/meterlist_thumb.c"', '"../ui/meterlist_thumb.c"'))
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.splitlines()
def decode(t):
    px = []
    for b in bytes.fromhex(t["rle"]): px += [b >> 5] * ((b & 31) + 1)
    return "".join(str(v) for v in px)
mans = sorted((json.loads(p.read_text()) for p in (ROOT / "meters").glob("*/meter.json")), key=lambda m: m.get("p2", {}).get("slot", 99))
mans = [m for m in mans if "p2" in m]
out = run(False)
ok_all = True
for m, d in enumerate(mans):
    t = json.loads((ROOT / "meters" / d["key"] / "thumb.json").read_text())
    ln = out[m].split()
    ok = ln[1] == decode(t) and [int(v) for v in ln[3:11]] == t["palette"] and ln[11:] == ["blit", str(m * 56), "740", str(20 + m), str(50 + m), "56", "32"]
    ok_all &= ok; check(f"{d['key']}: the plane equals the decoded preview, its own palette, the blit to its position", ok)
check("every call stayed inside the plane and the palette starts at index 0", out[-1] == "calls %d bad 0" % (2 * 0 + len(mans) * 2), out[-1])
bad = run(True)
check("mutant (runs not wrapped at the row end) is caught", bad != out)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
