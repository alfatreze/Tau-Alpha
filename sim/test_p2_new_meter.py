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
    tc = ROOT / "toolchain"
    env = dict(os.environ, RISCV_TOOLCHAIN_BIN=str(next(tc.glob("*/bin")))) if tc.exists() and list(tc.glob("*/bin")) else dict(os.environ)
    def make(*args): return subprocess.run(["make", "new-meter"] + list(args), cwd=cp, capture_output=True, text=True, env=env)
    before = sorted(str(p.relative_to(cp)) for p in cp.rglob("*") if p.is_file())
    r = make("KEY=winamp_bars", "TAG=ZZ")
    check("a key that exists is refused", r.returncode != 0 and "exists" in (r.stdout + r.stderr))
    r = make("KEY=Bad-Key"); check("a bad key is refused", r.returncode != 0)
    r = make("KEY=sparks", "TAG=BARS"); check("a taken tag is refused", r.returncode != 0 and "taken" in (r.stdout + r.stderr))
    check("a refused scaffold writes nothing", before == sorted(str(p.relative_to(cp)) for p in cp.rglob("*") if p.is_file()))
    r = make("KEY=sparks", "TAG=SPK", "LABEL=SPARKS")
    check("the scaffold runs, generates the tables and builds the pack", r.returncode == 0 and "pack_meter.py sparks" in r.stdout, r.stdout[-600:] + r.stderr[-600:])
    if r.returncode == 0:
        m = json.loads((cp / "meters/sparks/meter.json").read_text()); old = [json.loads(p.read_text()) for p in (ROOT / "meters").glob("*/meter.json")]
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
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
