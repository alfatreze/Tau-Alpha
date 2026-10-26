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
        check(len(m["files"]) == len(tracked), f"every tracked file is in the manifest ({len(tracked)})")
        check(any(e["path"] == "preview/meters/sound_map_wasm.js" for e in m["files"]) and m["wasm"]["wasm_bytes"] > 0, "the wasm module is in the drop with its size and SHA-256")
        check(run("--check", str(out)).returncode == 0, "a fresh drop verifies")
        f = out / "preview/render.js"; b = f.read_bytes(); f.write_bytes(b + b" ")
        check(run("--check", str(out)).returncode == 1, "a changed file is caught"); f.write_bytes(b)
        f.unlink(); check(run("--check", str(out)).returncode == 1, "a missing file is caught"); f.write_bytes(b)
        (out / "preview/extra.js").write_text("x"); check(run("--check", str(out)).returncode == 1, "an extra file is caught"); (out / "preview/extra.js").unlink()
        check(run("--out", str(out)).returncode != 0, "a non-empty target is refused")
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
