#!/usr/bin/env python3
"""Meter preset bookkeeping (fw2/ui/meters.c): select validates (out of range = the first, an unknown meter is ignored), next wraps at each meter's own count, the module is told every change, and the names come from the module.
Mutants: next skips one, no range check."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
sys.path.insert(0, str(ROOT / "tools"))
import tau_assets as ta
def build(td, mutant):
    d = Path(td)
    for sub in ("ui", "gfx", "platform", "cores", "generated", "sim"): (d / sub).mkdir()
    src = (ROOT / "fw2/ui/meters.c").read_text()
    if mutant == "skips": a = "(pre[cur] + 1u) % meters_npre(cur)"; assert a in src; src = src.replace(a, "(pre[cur] + 2u) % meters_npre(cur)")
    if mutant == "norange": a = "if (p >= meters_npre(m)) p = 0u;"; assert a in src; src = src.replace(a, "")
    if mutant == "offbyone": a = "const uint32_t at = e.off + p * e.psize"; assert a in src; src = src.replace(a, "const uint32_t at = e.off + 1u + p * e.psize")
    (d / "ui/meters.c").write_text(src)
    for f in ("ui/meter_pack.h", "cores/meter_desc_core.h", "cores/meter_pack_core.h", "cores/library_core.h", "ui/meter.h", "ui/theme.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/pack_strike_core.h", "cores/audio_stats_core.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"): (d / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_mpre_harness.c").read_text().replace('"../fw2/ui/meters.c"', '"../ui/meters.c"'))
    (d / "sim/p2_dyn_register.h").write_text((ROOT / "sim/p2_dyn_register.h").read_text())
    sys.path.insert(0, str(ROOT / "tools/player2")); import meter_desc as _md
    for k in ("winamp_scope", "chladni", "layered_wave", "vu_master", "waterfall_grid"): (d / ("%s.desc" % k)).write_bytes(_md.for_meter(k))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d
import json
def man(k): return json.loads((ROOT / "meters" / k / "meter.json").read_text())
DESCS = ["winamp_scope", "chladni", "layered_wave", "vu_master", "waterfall_grid"]
def vals(k, p): d = man(k); return " ".join(str(d["presets"][p]["values"][x["key"]]) for x in d["params"])
GOOD = ["rows 5 count 6", "of3 6 of2 0", "mv3 " + vals("layered_wave", 6), "chladni_after5 1 name " + man("chladni")["presets"][1]["name"], "mv2 " + vals("chladni", 1), "vu_wrap 0", "bars_next 1 last0 1"]
def run_plain(d): return subprocess.run([str(d / "h"), "-"] + [str(d / ("%s.desc" % k)) for k in DESCS], capture_output=True, text=True).stdout.strip().split("\n")
KEYS = (("winamp_bars", 3), ("winamp_scope", 2), ("chladni", 4), ("layered_wave", 5), ("vu_master", 2), ("waterfall_grid", 3))      # every meter: Winamp Bars is built in, the others are rows through their descriptors
def metr_spec():
    reg = ta.registry(); ms = []
    for key, n in KEYS:
        ms.append({"key": key, "default_preset": n - 1, "presets": [{"name": "USER %d" % i, "values": dict(p["values"])} for i, p in enumerate(reg[key]["presets"][:n])]})
    return {"meters": ms}
def metr_expected(spec):
    reg = ta.registry(); out = ["loaded %d" % len(KEYS)]
    for m, e in enumerate(spec["meters"]):
        keys = [p["key"] for p in reg[e["key"]]["params"]]
        out.append(f"meter {m} npre {len(e['presets'])} boot {e['default_preset']}")
        for p in e["presets"]: out.append(p["name"] + "".join(" %d" % p["values"][k] for k in keys))
    return out
def run_metr(d, blob):
    f = d / "metr.bin"; f.write_bytes(blob)
    return subprocess.run([str(d / "h"), str(f)] + [str(d / ("%s.desc" % k)) for k in DESCS], capture_output=True, text=True).stdout.strip().split("\n")
with tempfile.TemporaryDirectory() as td:
    d = build(td, None)
    check("select validates, next wraps per meter, names come from the module", run_plain(d) == GOOD)
    spec = metr_spec(); blob = ta.pack_meters(spec)
    check("a METR file replaces the presets: counts, names, values and boot presets equal the Python writer's", run_metr(d, blob) == metr_expected(spec))
for m in ("skips", "norange"):
    with tempfile.TemporaryDirectory() as td: check(f"mutant {m} is caught", run_plain(build(td, m)) != GOOD)
with tempfile.TemporaryDirectory() as td:
    d = build(td, "offbyone"); check("mutant offbyone (a preset is read from the wrong byte) is caught", run_metr(d, ta.pack_meters(metr_spec())) != metr_expected(metr_spec()))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
