#!/usr/bin/env python3
"""`make new-meter` (tools/player2/new_meter.py; B-814): run in a COPY of the tree, it creates a pack-only meter (manifest with the next slot and manifest index, preview, drawing source using the cell grid and the history ring
helpers, pack source), runs the generators and builds the pack; the new meter then passes the pack test (sim/test_p2_meter_pack.py picks it up from its manifest: generic trace over every preset, loud and quiet
stretches, a pause, the degrade levels, the fullscreen box; the pack run on rv32sim must draw exactly what the source compiled straight in draws, inside its box, and the descriptor it carries must parse). A key that
exists, a bad key and a taken tag are refused, and nothing is written for a refused one."""
import json, os, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:600]))
    fails += 0 if ok else 1
def ignore(d, names): return [n for n in names if n in ("work", ".git", "__pycache__", "dist", "docs", "release", "node_modules")]
with tempfile.TemporaryDirectory() as td:
    cp = Path(td) / "tree"; cp.mkdir()
    for part in ("fw", "fw2", "tools", "sim", "meters", "themes"):
        if (ROOT / part).exists(): shutil.copytree(ROOT / part, cp / part, ignore=ignore, symlinks=True)
    shutil.copy(ROOT / "Makefile", cp / "Makefile")
    (cp / "docs").mkdir()                                                          # tools/gen_meters.py writes its tables there
    (cp / "dist/Cores/alfatreze.TAU").mkdir(parents=True); shutil.copy(ROOT / "dist/Cores/alfatreze.TAU/interact.json", cp / "dist/Cores/alfatreze.TAU/interact.json")
    # the seven slots are all in use (the last one is SOUND MAP): the copy gives its slot back so the scaffold has somewhere to go
    shutil.rmtree(cp / "meters/sound_map")
    for f in ("fw2/ui/meter_sound_map.c", "fw2/ui/meter_pack_sound_map.c", "fw2/generated/desc_sound_map.h", "tools/meters/preview/wasm/sound_map_wasm.c", "tools/meters/preview/meters/sound_map.js", "tools/meters/preview/meters/sound_map_wasm.js"): (cp / f).unlink()
    sys.path.insert(0, str(ROOT / "tools")); import toolchain
    env = toolchain.host_env()                                                      # a working SDKROOT; the RISC-V toolchain of the main checkout when this is a worktree
    try: env["RISCV_TOOLCHAIN_BIN"] = str(toolchain.riscv_bin())
    except toolchain.ToolchainError: pass
    def make(*args): return subprocess.run(["make", "new-meter"] + list(args), cwd=cp, capture_output=True, text=True, env=env)
    for g in ("tools/player2/gen_meters_p2.py", "tools/gen_meters.py"):
        gr = subprocess.run([sys.executable, g], cwd=cp, capture_output=True, text=True, env=env)
        if gr.returncode: print(gr.stdout[-600:] + gr.stderr[-600:]); sys.exit(1)
    before = sorted(str(p.relative_to(cp)) for p in cp.rglob("*") if p.is_file())
    r = make("KEY=winamp_bars", "TAG=ZZ")
    check("a key that exists is refused", r.returncode != 0 and "exists" in (r.stdout + r.stderr))
    r = make("KEY=Bad-Key"); check("a bad key is refused", r.returncode != 0)
    r = make("KEY=sparks", "TAG=BARS"); check("a taken tag is refused", r.returncode != 0 and "taken" in (r.stdout + r.stderr))
    check("a refused scaffold writes nothing", before == sorted(str(p.relative_to(cp)) for p in cp.rglob("*") if p.is_file()))
    r = make("KEY=sparks", "TAG=SPK", "LABEL=SPARKS")
    check("the scaffold runs, generates the tables and builds the pack", r.returncode == 0 and "pack_meter.py sparks" in r.stdout, r.stdout[-600:] + r.stderr[-600:])
    if r.returncode == 0:
        m = json.loads((cp / "meters/sparks/meter.json").read_text()); old = [json.loads(p.read_text()) for p in (cp / "meters").glob("*/meter.json") if "sparks" not in str(p)]
        check("its manifest takes the next slot and manifest index, is pack-only, and its limits are set", m["p2"]["slot"] == max(o["p2"]["slot"] for o in old if "p2" in o) + 1 and m["index"] == max(o["index"] for o in old) + 1 and m["p2"]["pack_only"] and m["p2"]["cmds"] > 0 and m["p2"]["max_cycles"] > 0 and m["p2"]["tag"] == "SPK", m["p2"])
        check("its files exist: preview, drawing source, pack source and the pack", all((cp / f).exists() for f in ("meters/sparks/thumb.json", "fw2/ui/meter_sparks.c", "fw2/ui/meter_pack_sparks.c", "work/packs-p2/sparks.tmpk")))
        reg = (cp / "fw2/generated/meter_registry.h").read_text()
        check("the registry lists it among the pack-only meters (Info row) and the METER list has room", 'X(%d, "SPARKS")' % m["p2"]["slot"] in reg and "#define METER_SLOTS %du" % (m["p2"]["slot"] + 1) in reg, reg[:900])
        src = (cp / "fw2/ui/meter_sparks.c").read_text()
        check("the drawing source uses the two helpers", 'meter_grid.h' in src and "histring_core.h" in src and "hr_unwrap" in src and "mgrid_put" in src)
        r2 = subprocess.run([sys.executable, "sim/test_p2_meter_pack.py"], cwd=cp, capture_output=True, text=True, env=env)
        lines = r2.stdout.splitlines()
        mine = [l for l in lines if l.startswith("ok   sparks") or l.startswith("FAIL sparks")]
        check("the pack test, run in the copy, covers the new meter and passes: native trace equals the pack on rv32sim, no draw outside the box, a descriptor", r2.returncode == 0 and len([l for l in mine if l.startswith("ok")]) >= 6 and not [l for l in lines if l.startswith("FAIL")], "\n".join(mine) + r2.stdout[-500:] + r2.stderr[-500:])
    # owner decision D-M15: the scaffold also writes the meter's WebAssembly wrapper and lab adapter, and (with the Homebrew llvm + lld) builds its module, which the golden test and the coverage test then check
    if r.returncode == 0:
        check("the scaffold writes the WebAssembly wrapper and the lab adapter", all((cp / f).exists() for f in ("tools/meters/preview/wasm/sparks_wasm.c", "tools/meters/preview/meters/sparks.js")))
        if (sys.path.insert(0, str(ROOT / "tools")), __import__("toolchain").wasm_tools())[1] and shutil.which("node"):
            check("the scaffold builds the module (meters/sparks_wasm.js)", (cp / "tools/meters/preview/meters/sparks_wasm.js").exists(), r.stdout[-500:])
            r3 = subprocess.run([sys.executable, "sim/test_meter_wasm_coverage.py"], cwd=cp, capture_output=True, text=True, env=env)
            check("the coverage test passes with the new meter (every Player 2 meter has a module)", r3.returncode == 0 and "sparks: the committed module is current" in r3.stdout, r3.stdout[-600:])
            r4 = subprocess.run([sys.executable, "sim/test_meter_wasm_golden.py"], cwd=cp, capture_output=True, text=True, env=env)
            check("the golden test picks the new meter up: the module and the source compiled natively issue the same commands", r4.returncode == 0 and "sparks: every scenario draws identically" in r4.stdout, r4.stdout[-800:] + r4.stderr[-400:])
        else: print("note: no wasm toolchain (Homebrew llvm + lld) or node: the module of the new meter was not built or checked")
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
