#!/usr/bin/env python3
"""Which handler draws each meter when packs are installed (PACKS build of fw2/ui/meters.c, host build with stub ticks): a meter with a valid pack runs its pack (and only then), Winamp Bars (the one built-in meter) whose pack is refused or missing keeps its built-in tick (the fallback), every other meter exists only through its pack (no pack, a refused pack or a damaged bundle: it is not listed), and the Info summary tells which. Mutants: a refused pack still runs; the built-in
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
    core = (ROOT / "fw2/cores/meter_pack_core.h").read_text()
    if mutant == "truncok": a = "return (off > MPKB_HDR && rd(ctx, off - 1u, win, 1u)) ? off : 0u;"; assert a in core; core = core.replace(a, "return off > MPKB_HDR ? off : 0u;")
    if mutant == "refused_runs": a = "static int pack_ok(uint32_t m) { return m < MTR_PACK_SLOTS && pack_slot[m].status == MPK_OK; }"; assert a in src; src = src.replace(a, "static int pack_ok(uint32_t m) { return m < MTR_PACK_SLOTS && pack_slot[m].status != 0xFFu; }")
    if mutant == "both": a = "    } else\n#endif\n    mdef(cur)->tick(&in);"; assert a in src; src = src.replace(a, "    }\n#endif\n    mdef(cur)->tick(&in);")
    ui = d / "fw2/ui"; ui.mkdir(parents=True)
    for sub in ("gfx", "platform", "cores", "generated", "diagnostics"): (d / "fw2" / sub).mkdir()
    (ui / "meters.c").write_text(src)
    for f in ("ui/meter.h", "ui/theme.h", "ui/meter_pack.h", "gfx/gfx.h", "platform/mmio.h", "platform/platform.h", "cores/spectrum_core.h", "cores/metr_core.h", "cores/tune_core.h", "cores/meter_budget_core.h", "cores/pack_strike_core.h", "cores/audio_stats_core.h", "cores/meter_desc_core.h", "cores/helios_core.h", "cores/gfx_touch_core.h", "cores/meter_pack_core.h", "cores/library_core.h", "generated/meter_presets.h", "generated/lw_presets.h", "generated/meter_registry.h"):
        (d / "fw2" / f).write_text(core if f == "cores/meter_pack_core.h" else (ROOT / "fw2" / f).read_text())
    (d / "sim").mkdir(); (d / "sim/h.c").write_text((ROOT / "sim/p2_meter_pack_dispatch_harness.c").read_text().replace('"../fw2/ui/meters.c"', '"../fw2/ui/meters.c"'))
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DP2_HOST", "-include", str(ROOT / "sim/p2_host_regs.h"), "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return d / "h"
def run(exe, bundle, file=False):
    args = [str(exe)] + ([str(bundle)] if bundle else []) + (["file"] if file else [])
    return subprocess.run(args, capture_output=True, text=True).stdout.strip().splitlines()
def parse(out):
    m = {}
    for ln in out:
        t = ln.split()
        if t[0].startswith("m"): m[int(t[0][1:])] = {"b": int(t[2]), "p": int(t[4]), "s": int(t[6]), "v": int(t[8])}
        elif t[0] == "info": m["info"] = {"run": int(t[2], 16), "err": int(t[4]), "file": int(t[6]), "seen": int(t[8])}; m["src"] = int(t[10])
        elif t[0] == "fileload": m["fileload"] = int(t[1])
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
    n_meters = len([k for k in parse(run(exe, None)) if isinstance(k, int)])
    none = parse(run(exe, None)); bars = pm.SLOT["winamp_bars"]; scope = pm.SLOT["winamp_scope"]; vu = pm.SLOT["vu_master"]
    absent = lambda r, m: r[m]["v"] == 0 and r[m]["p"] == 0 and r[m]["b"] == 0
    check("no bundle: Winamp Bars (the one built-in meter) draws with its built-in tick, every pack-only meter is absent, and nothing is reported as a pack", none[bars]["b"] == 2 and none[bars]["p"] == 0 and none[bars]["s"] == 255 and all(absent(none, m) for m in range(n_meters) if m != bars) and none["info"] == {"run": 0, "err": 0, "file": 255, "seen": 0}, str(none))
    allp = parse(run(exe, bundle_of(list(packs.values()), "all.bin")))
    exp = {pm.SLOT[k] for k in packs}
    check("three packs valid: Winamp Bars runs its pack (twice over two passes) and never its built-in, the Scope and Master VU are rows that run their packs, the meters without a pack are absent",
          all((allp[m]["v"] == 1 and allp[m]["p"] == 2 and allp[m]["b"] == 0 and allp[m]["s"] == 0) if m in exp else absent(allp, m) for m in range(n_meters)), str(allp))
    check("the Info summary: packs running as a bit per meter, no refusal, three packs seen", allp["info"] == {"run": sum(1 << m for m in exp), "err": 0, "file": 0, "seen": 3}, str(allp["info"]))
    bad = bytearray(packs["winamp_scope"]); bad[60] ^= 0x10
    part = parse(run(exe, bundle_of([packs["winamp_bars"], bytes(bad), packs["vu_master"]], "bad.bin")))
    check("one pack corrupt (E26): that pack-only meter is not listed, the other two still run as packs",
          absent(part, scope) and part[scope]["s"] == 26 and part[bars]["p"] == 2 and part[vu]["p"] == 2 and part["info"]["err"] == 26 and part["info"]["run"] == sum(1 << pm.SLOT[k] for k in ("winamp_bars", "vu_master")), str(part))
    badbars = bytearray(packs["winamp_bars"]); badbars[60] ^= 0x10
    pb2 = parse(run(exe, bundle_of([bytes(badbars), packs["winamp_scope"]], "badbars.bin")))
    check("the built-in meter's pack corrupt (E26): Winamp Bars falls back to its built-in tick, the Scope still runs", pb2[bars]["p"] == 0 and pb2[bars]["b"] == 2 and pb2[bars]["s"] == 26 and pb2[scope]["p"] == 2, str(pb2))
    garbage = tmp / "garbage.bin"; garbage.write_bytes(b"NOPE" + b"\0" * 100)
    g = parse(run(exe, garbage))
    check("a file that is not a bundle (E30): Winamp Bars keeps its built-in, no pack-only meter is listed", g[bars]["p"] == 0 and g[bars]["b"] == 2 and all(absent(g, m) for m in range(n_meters) if m != bars) and g["info"]["file"] == 30, str(g))
    old = bytearray(packs["vu_master"]); old[4] = 3                    # a pack that needs a newer ABI minor
    o = parse(run(exe, bundle_of([bytes(old)], "old.bin")))
    check("a pack of another ABI is refused (E22) and its meter is not listed", o[vu]["s"] == 22 and absent(o, vu), str(o))
    with tempfile.TemporaryDirectory() as t2:
        m1 = parse(run(build(Path(t2), "refused_runs"), bundle_of([bytes(badbars), packs["winamp_scope"]], "b2.bin")))
        check("mutant (a refused pack still runs) is caught", m1[bars]["b"] != 2)
    with tempfile.TemporaryDirectory() as t3:
        m2 = parse(run(build(Path(t3), "both"), bundle_of(list(packs.values()), "b3.bin")))
        check("mutant (built-in draws as well as the pack) is caught", m2[bars]["b"] != 0)
    # the core's own tau-packs.bin (data slot 9): read through the slot in pieces, the same bundles install the same way as from memory
    def same(a, b): return {k: v for k, v in a.items() if k not in ("src", "fileload")} == {k: v for k, v in b.items() if k not in ("src", "fileload")}
    bundles = [("all.bin", None), ("bad.bin", None), ("badbars.bin", None), ("old.bin", None)]
    ok = True; info = ""
    for name in ("all.bin", "bad.bin", "badbars.bin", "old.bin"):
        mem, fil = parse(run(exe, tmp / name)), parse(run(exe, tmp / name, True))
        if not (same(mem, fil) and fil.get("fileload") == 1 and fil["src"] == 2 and mem["src"] == 1): ok = False; info = name
    check("a bundle read from the core's own file (data slot 9) installs exactly as from the MPKB section, and says where it came from (src 2 against 1)", ok, info)
    nofile = parse(run(exe, tmp / "missing.bin", True))
    check("no tau-packs.bin: the loader reports none (the caller falls back to tau-assets.bin), nothing is installed", nofile.get("fileload") == 0 and nofile["src"] == 0 and nofile[bars]["b"] == 2 and all(absent(nofile, m) for m in range(n_meters) if m != bars), str(nofile))
    full = (tmp / "all.bin").read_bytes(); (tmp / "cut.bin").write_bytes(full[:-10])
    cut = parse(run(exe, tmp / "cut.bin", True)); cut_mem = parse(run(exe, tmp / "cut.bin"))
    check("a file cut short is refused whole (no partial install, the fallback applies); the memory loader keeps what came before", cut.get("fileload") == 0 and cut["src"] == 0 and all(absent(cut, m) for m in range(n_meters) if m != bars) and cut_mem["info"]["seen"] >= 1, str(cut))
    (tmp / "junk.bin").write_bytes(b"NOPE" + b"\0" * 100)
    junk = parse(run(exe, tmp / "junk.bin", True))
    check("a file that is not a bundle is no bundle for the file loader (the fallback applies)", junk.get("fileload") == 0 and junk["src"] == 0, str(junk))
    with tempfile.TemporaryDirectory() as t4:
        mt = parse(run(build(Path(t4), "truncok"), tmp / "cut.bin", True))
        check("mutant (the length walk does not require the last byte) is caught", mt.get("fileload") != 0)
print("PASSED" if not fails else "FAILED (%d)" % fails)
sys.exit(1 if fails else 0)
