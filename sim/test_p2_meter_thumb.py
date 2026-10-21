#!/usr/bin/env python3
"""The METER list previews (fw2/ui/meterlist_thumb.c): the plane built in the stash equals an independent decode of each meters/<key>/thumb.json, the palette loaded is that meter's own resolved from the theme roles (a role, a ramp between two roles, or the fixed colour), and the blit takes
the 56x32 plane to the requested position. Mutants: runs that cross a row end are not wrapped; a ramp ignores its second role."""
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
        if mutant == True:
            a = "const uint32_t n = run < MTH_W - x ? run : MTH_W - x;"; assert a in src; src = src.replace(a, "const uint32_t n = run;")
        if mutant == "ramp":
            a = "rc_ramp(th_role[e->a], th_role[e->b], e->t, e->n)"; assert a in src; src = src.replace(a, "rc_ramp(th_role[e->a], th_role[e->a], e->t, e->n)")
        (d / "ui/meterlist_thumb.c").write_text(src)
        (d / "cores").mkdir()
        for f in ("ui/meterlist_thumb.h", "ui/theme.h", "cores/ramp_core.h", "gfx/gfx.h", "generated/meter_registry.h", "generated/meter_thumbs.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
        (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_thumb_harness.c").read_text().replace('"../fw2/ui/meterlist_thumb.c"', '"../ui/meterlist_thumb.c"'))
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        return subprocess.run([str(d / "h")], capture_output=True, text=True).stdout.splitlines()
ROLES = ["bg_top", "bg_bottom", "surface", "surface_track", "text_primary", "text_secondary", "accent", "on_accent", "accent2", "ok", "warn", "danger", "base", "chrome", "pill", "error", "faint", "splash_bg", "splash_bar", "fs_red", "fs_track"]
def role(name): return (ROLES.index(name) * 0x0841 + 0x1234) & 0xFFFF                 # the harness's role colours
def ramp(a, b, t, n):                                                                   # an independent twin of rc_ramp (RGB565 channels, truncating)
    out = 0
    for sh, w in ((11, 5), (5, 6), (0, 5)):
        m = (1 << w) - 1; ca, cb = (a >> sh) & m, (b >> sh) & m; out |= ((ca * (n - t) + cb * t) // n) << sh
    return out
def expected_palette(t):
    out = []
    for fixed, sp in zip(t["palette"], t.get("palette_roles", [None] * 8)):
        out.append(fixed if sp is None else role(sp) if isinstance(sp, str) else ramp(role(sp["ramp"][0]), role(sp["ramp"][1]), sp["ramp"][2], sp["ramp"][3]))
    return out
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
    ok = ln[1] == decode(t) and [int(v) for v in ln[3:11]] == expected_palette(t) and ln[11:] == ["blit", str(m * 56), "740", str(20 + m), str(50 + m), "56", "32"]
    ok_all &= ok; check(f"{d['key']}: the plane equals the decoded preview, its own palette, the blit to its position", ok)
check("every call stayed inside the plane and the palette starts at index 0", out[-1] == "calls %d bad 0" % (2 * 0 + len(mans) * 2), out[-1])
bad = run(True)
check("mutant (runs not wrapped at the row end) is caught", bad != out)
check("mutant (a ramp ignores its second role) is caught", run("ramp") != out)
check("the previews really follow the theme: every meter has at least one role-driven colour", all(any(sp is not None for sp in json.loads((ROOT / "meters" / d["key"] / "thumb.json").read_text()).get("palette_roles", [])) for d in mans))
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
