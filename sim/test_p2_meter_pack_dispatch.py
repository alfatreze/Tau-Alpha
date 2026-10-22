#!/usr/bin/env python3
"""Which handler draws each meter when packs are installed (PACKS build of fw2/ui/meters.c, host build with stub ticks): a meter with a valid pack runs its pack (and only then), a meter whose pack is refused
or missing keeps its built-in tick (the per-meter fallback), a damaged bundle or none leaves every built-in meter in use, and the Info summary tells which. Mutants: a refused pack still runs; the built-in
runs as well as the pack."""
import struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import pack_meter as pm, pack_bundle as pb
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("  " + info if info and not ok else ""))
    fails += 0 if ok else 1
def build(d, mutant=None):
    src = (ROOT / "fw2/ui/meters.c").read_text()
    if mutant == "refused_runs": a = "static int pack_ok(uint32_t m) { return m < MTR_PACK_SLOTS && pack_slot[m].status == MPK_OK; }"; assert a in src; src = src.replace(a, "static int pack_ok(uint32_t m) { return m < MTR_PACK_SLOTS && pack_slot[m].status != 0xFFu; }")
    if mutant == "both": a = "    } else\n#endif\n    meter_table[cur].tick(&in);"; assert a in src; src = src.replace(a, "    }\n#endif\n    meter_table[cur].tick(&in);")
    ui = d / "fw2/ui"; ui.mkdir(parents=True)
    for sub in ("gfx", "platform", "cores", "generated", "diagnostics"): (d / "fw2" / sub).mkdir()
    (ui / "meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "ui/meter_pack.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/pack_strike_core.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "cores/meter_pack_core.h", "cores/library_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"):
        (d / "fw2" / f).write_text((ROOT / "fw2" / f).read_text())
    (d / "sim").mkdir(); (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_pack_dispatch_harness.c").read_text().replace('"../fw2/ui/meters.c"', '"../fw2/ui/meters.c"'))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def run(exe, bundle):
    args = [str(exe)] + ([str(bundle)] if bundle else [])
    return subprocess.run(args, capture_output=True, text=True).stdout.strip().splitlines()
def parse(out):
    m = {}
    for ln in out:
        t = ln.split()
        if t[0].startswith("m"): m[int(t[0][1:])] = {"b": int(t[2]), "p": int(t[4]), "s": int(t[6])}
        elif t[0] == "info": m["info"] = {"run": int(t[2], 16), "err": int(t[4]), "file": int(t[6]), "seen": int(t[8])}
    return m
with tempfile.TemporaryDirectory() as td:
    tmp = Path(td)
    packs = {}
    for key in ("winamp_bars", "winamp_scope", "vu_master"):
        blob, _ = pm.build(key, pm.SLOT_BASE + pm.SLOT[key] * pm.SLOT_SIZE, tmp / (key + ".elf"))
        packs[key] = blob
    exe = build(tmp / "good")
    def bundle_of(blobs, name):
        p = tmp / name; p.write_bytes(pb.bundle(blobs)); return p
    n_meters = len(parse(run(exe, None))) - 1
    none = parse(run(exe, None))
    check("no bundle: every meter keeps its built-in tick and nothing is reported as a pack", all(none[m]["b"] == 2 and none[m]["p"] == 0 and none[m]["s"] == 255 for m in range(n_meters)) and none["info"] == {"run": 0, "err": 0, "file": 255, "seen": 0}, str(none))
    allp = parse(run(exe, bundle_of(list(packs.values()), "all.bin")))
    exp = {pm.SLOT[k] for k in packs}
    check("all three packs valid: those meters run their pack (twice over two passes) and never the built-in; the others keep theirs",
          all((allp[m]["p"] == 2 and allp[m]["b"] == 0 and allp[m]["s"] == 0) if m in exp else (allp[m]["p"] == 0 and allp[m]["b"] == 2) for m in range(n_meters)), str(allp))
    check("the Info summary: packs running as a bit per meter, no refusal, three packs seen", allp["info"] == {"run": sum(1 << m for m in exp), "err": 0, "file": 0, "seen": 3}, str(allp["info"]))
    bad = bytearray(packs["winamp_scope"]); bad[60] ^= 0x10
    part = parse(run(exe, bundle_of([packs["winamp_bars"], bytes(bad), packs["vu_master"]], "bad.bin")))
    s_scope = pm.SLOT["winamp_scope"]
    check("one pack corrupt (E26): that meter falls back to its built-in, the other two still run as packs",
          part[s_scope]["p"] == 0 and part[s_scope]["b"] == 2 and part[s_scope]["s"] == 26 and part[pm.SLOT["winamp_bars"]]["p"] == 2 and part[pm.SLOT["vu_master"]]["p"] == 2 and part["info"]["err"] == 26 and part["info"]["run"] == sum(1 << pm.SLOT[k] for k in ("winamp_bars", "vu_master")), str(part))
    garbage = tmp / "garbage.bin"; garbage.write_bytes(b"NOPE" + b"\0" * 100)
    g = parse(run(exe, garbage))
    check("a file that is not a bundle (E30): every meter keeps its built-in", all(g[m]["p"] == 0 and g[m]["b"] == 2 for m in range(n_meters)) and g["info"]["file"] == 30, str(g))
    old = bytearray(packs["vu_master"]); old[4] = 1                    # an older ABI
    o = parse(run(exe, bundle_of([bytes(old)], "old.bin")))
    check("a pack of another ABI is refused (E22) and the built-in keeps drawing", o[pm.SLOT["vu_master"]]["s"] == 22 and o[pm.SLOT["vu_master"]]["p"] == 0 and o[pm.SLOT["vu_master"]]["b"] == 2, str(o))
    with tempfile.TemporaryDirectory() as t2:
        m1 = parse(run(build(Path(t2), "refused_runs"), bundle_of([packs["winamp_bars"], bytes(bad)], "b2.bin")))
        check("mutant (a refused pack still runs) is caught", m1[s_scope]["b"] != 2)
    with tempfile.TemporaryDirectory() as t3:
        m2 = parse(run(build(Path(t3), "both"), bundle_of(list(packs.values()), "b3.bin")))
        check("mutant (built-in draws as well as the pack) is caught", any(m2[m]["b"] != 0 for m in exp))
print("PASSED" if not fails else "FAILED (%d)" % fails)
sys.exit(1 if fails else 0)
