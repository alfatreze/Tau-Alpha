#!/usr/bin/env python3
"""Pack-only meters (B-812, roadmap item 4): the Waterfall exists in the firmware only through its pack. With the pack installed (through the real loader) it is a row whose name, Configure descriptors, presets, compose
code, limits and preview come from the descriptor the pack carries, equal to what the manifest says; without the pack (no bundle, a damaged or missing descriptor) the row is absent: the METER list is the built-in
meters, X skips nothing and selecting the row falls back to the first meter; the saved preset of an absent meter is kept. Mutants: the row is listed without a pack; a damaged descriptor is accepted."""
import json, struct, subprocess, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import pack_meter as pm, pack_bundle as pb, meter_desc as md
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
LISTS = ["cores/pack_strike_core.h", "cores/meter_desc_core.h", "cores/meter_budget_core.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "cores/meter_pack_core.h", "cores/library_core.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h"]
def build(td, mutant=None):
    d = Path(td); d.mkdir(parents=True, exist_ok=True)
    for sub in ("ui", "gfx", "platform", "cores", "generated", "sim"): (d / "fw2" / sub).mkdir(parents=True, exist_ok=True)
    (d / "sim").mkdir(exist_ok=True)
    src = (ROOT / "fw2/ui/meters.c").read_text()
    if mutant == "listed": a = "int meters_present(uint32_t m) { return m < METER_N || (m < METER_SLOTS && dyn[m - METER_N].ok && pack_slot[m].status != 0xFFu); }"; assert a in src; src = src.replace(a, "int meters_present(uint32_t m) { return m < METER_SLOTS; }")
    if mutant == "baddesc": a = "if (md_check(desc_rd, d, len, TR_COUNT, &h) || h.slot != m)"; assert a in src; src = src.replace(a, "if (!md_header(desc_rd, d, len, &h))")
    (d / "fw2/ui/meters.c").write_text(src)
    for f in ["ui/meter.h", "ui/theme.h", "ui/meter_pack.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"] + LISTS: (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_pack_only_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def run(exe, bundle=None): return subprocess.run([str(exe)] + ([str(bundle)] if bundle else []), capture_output=True, text=True).stdout.strip().splitlines()
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td); exe = build(tmp / "good")
    wf = json.loads((ROOT / "meters/waterfall_grid/meter.json").read_text()); slot = wf["p2"]["slot"]
    blob, _ = pm.build("waterfall_grid", pm.SLOT_BASE + slot * pm.SLOT_SIZE, tmp / "wf.elf")
    bundle = tmp / "b.bin"; bundle.write_bytes(pb.bundle([blob]))
    n_static = len([1 for p in (ROOT / "meters").glob("*/meter.json") if "p2" in json.loads(p.read_text()) and not json.loads(p.read_text())["p2"].get("pack_only")])
    check("the Waterfall's manifest is pack-only and takes the first slot after the built-in meters", wf["p2"].get("pack_only") and slot == n_static, slot)
    o = run(exe, bundle)
    check("with its pack installed the Waterfall is a row and the METER list holds every meter", o[0] == "present 1 count %d list %d state 0" % (n_static + 1, n_static + 1), o[0])
    check("selecting the row selects it, X walks every row and wraps, and the last row follows the first when going back", o[1] == "select row %d -> cur %d" % (n_static, n_static) and [l for l in o if l.startswith("next")] == ["next -> %d" % i for i in list(range(1, n_static + 1)) + [0]] and "prev from 0 -> %d" % n_static in o, o[1:10])
    rows = md.param_rows(wf)
    want_name = "name %s np %d npre %d cmp %d" % (wf["p2"].get("label", wf["name"]), len(rows), len(wf["presets"]), md.cmp_code_int(wf))
    check("its name, parameter count, preset count and compose code are the manifest's", want_name in o, [l for l in o if l.startswith("name")])
    pds = [l for l in o if l.startswith("pd ")]
    want = ["pd %s %d %d %d %d %d %d %d %d" % (r["label"], r["type"], r["hidden"], r["min"], r["max"], r["step"], len(r["vals"]), r["when_idx"], r["when_val"]) + "".join(" [%s]" % v for v in r["vals"]) for r in rows]
    check("its Configure descriptors (label, type, range, step, choices, when) are the manifest's", pds == want, pds[:2])
    keys = [p["key"] for p in wf["params"]]
    prs = [l for l in o if l.startswith("preset ") and l.split()[1].isdigit()]
    check("its presets (names and values) are the manifest's and selecting one fills the live parameters", prs == ["preset %d %s %s" % (k, p["name"], " ".join(str(p["values"][key]) for key in keys)) for k, p in enumerate(wf["presets"])], prs[:2])
    check("it ticks as a pack, its manifest id goes into the config export, and its preview stream is readable", "ticks pack 2 builtin-text 0" in o and "payload id %d n %d" % (wf["index"], 4 + len(keys) + sum(1 for r in rows if r["type"] == 3 or (r["type"] == 0 and r["max"] > 255))) in o and "thumb len %d" % len(bytes.fromhex(json.loads((ROOT / "meters/waterfall_grid/thumb.json").read_text())["rle"])) in o, o[-4:])
    none = run(exe)
    check("without a bundle the row is absent: the list is the built-in meters", none[0] == "present 0 count %d list %d state 255" % (n_static, n_static), none[0])
    check("selecting the absent row falls back to the first meter and X never lands on it", none[1] == "select row %d -> cur 0" % n_static and all(l == "next -> %d" % ((i + 1) % n_static) for i, l in enumerate([l for l in none if l.startswith("next")][:n_static])), none[1:9])
    check("the saved preset of an absent meter is kept (not reset to 0), so it comes back with the pack", "preset of absent/present row after select 3: 3" in none, none[-1])
    # a pack whose descriptor is damaged but whose CRC is right: refused (41), the meter is not listed
    bad = bytearray(blob); dl = struct.unpack("<I", bad[-8:-4])[0]; at = len(bad) - dl + 40 + 16 * 0 + 6          # the first parameter's min field
    bad[at:at + 2] = (9000).to_bytes(2, "little"); body = bytes(bad[48:]); struct.pack_into("<I", bad, 20, zlib.crc32(body) & 0xFFFFFFFF)
    bb = tmp / "bad.bin"; bb.write_bytes(pb.bundle([bytes(bad)]))
    o2 = run(exe, bb)
    check("a pack whose descriptor is damaged (its CRC right) is refused with 41 and the meter is not listed", o2[0] == "present 0 count %d list %d state 41" % (n_static, n_static), o2[0])
    for m, name in (("listed", "the row is listed without a pack"), ("baddesc", "a damaged descriptor is accepted")):
        e2 = build(tmp / m, m)
        check("mutant (%s) is caught" % name, (run(e2, None) == none) is False if m == "listed" else (run(e2, bb) == o2) is False)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
