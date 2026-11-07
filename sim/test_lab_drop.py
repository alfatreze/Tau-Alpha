#!/usr/bin/env python3
"""tools/make_lab_drop.py: a drop lists every tracked file of the Meter Lab with its SHA-256, includes the SOUND MAP wasm module with its own record, verifies clean, and a changed, missing or extra file is caught."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(a, b):
    global fails
    name, ok = (a, b) if isinstance(a, str) else (b, a)
    print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1
def run(*a): return subprocess.run([sys.executable, str(ROOT / "tools/make_lab_drop.py"), *a], capture_output=True, text=True, cwd=ROOT)
dirty = subprocess.run(["git", "status", "--porcelain", "--", "tools/meters/preview"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
with tempfile.TemporaryDirectory() as td:
    out = Path(td) / "drop"
    r = run("--out", str(out))
    if dirty:
        check("a dirty tools/meters/preview is refused", r.returncode != 0 and "uncommitted" in r.stdout + r.stderr)
    else:
        check("a clean tree makes a drop", r.returncode == 0)
        m = json.loads((out / "MANIFEST.json").read_text())
        tracked = [f for f in subprocess.run(["git", "ls-files", "tools/meters/preview"], capture_output=True, text=True, cwd=ROOT).stdout.split("\n") if f]
        check(len(m["files"]) == len(tracked) + 2, f"every tracked file is in the manifest ({len(tracked)}) plus the built meter_lab.html and lab-data.json")
        check(any(e["path"] == "preview/meters/sound_map_wasm.js" for e in m["files"]) and m["wasm"]["wasm_bytes"] > 0, "the wasm module is in the drop with its size and SHA-256")
        mods = {x["module"]: x for x in m["modules"]}
        wasm_files = sorted(f for f in tracked if f.endswith("_wasm.js"))
        check(len(mods) == len(wasm_files) >= 8 and all(x["wasm_sha256"] and x["wasm_bytes"] > 0 and x["clang"] for x in mods.values()), f"EVERY wasm module has its SHA-256, size and compiler as plain fields ({len(mods)} modules)")
        ld = json.loads((out / "lab-data.json").read_text())
        keys = {x["key"] for x in ld["schema"]["meters"]}
        check({"winamp_bars", "winamp_scope", "chladni", "layered_wave", "vu_master", "waterfall_grid", "sound_map"} <= keys and ld["themes"] and ld["palette"] and all(x["params"] is not None and "presets" in x for x in ld["schema"]["meters"]), "lab-data.json has the meters (parameters, factory presets), the themes and the accent palette")
        html = (out / "meter_lab.html").read_text()
        check("/*DATA*/" not in html and "/*SCRIPTS*/" not in html and html.count("<script") >= 1 and len(html) > 100000, "the built meter_lab.html has no unresolved placeholder")
        check(m.get("contract") == 1 and ld.get("contract") == 1 and set(ld["views"]) == {"rects", "meters"}, "the manifest and lab-data.json carry contract 1 and the views", (m.get("contract"), ld.get("contract")))
        check(run("--check", str(out)).returncode == 0, "a fresh drop verifies")
        mf = out / "preview/meters/winamp_bars_wasm.js"; mb = mf.read_bytes(); mf.write_bytes(mb.replace(b"b64: \"", b"b64: \"AAAA", 1))
        check(run("--check", str(out)).returncode == 1, "a module whose bytes no longer match its record is caught"); mf.write_bytes(mb)
        f = out / "preview/render.js"; b = f.read_bytes(); f.write_bytes(b + b" ")
        check(run("--check", str(out)).returncode == 1, "a changed file is caught"); f.write_bytes(b)
        f.unlink(); check(run("--check", str(out)).returncode == 1, "a missing file is caught"); f.write_bytes(b)
        (out / "preview/extra.js").write_text("x"); check(run("--check", str(out)).returncode == 1, "an extra file is caught"); (out / "preview/extra.js").unlink()
        check(run("--out", str(out)).returncode != 0, "a non-empty target is refused")
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
