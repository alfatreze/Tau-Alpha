#!/usr/bin/env python3
"""Player 2, stage S0 (docs/features/PLAYER_V2_SPEC.md section 16): pin the baselines every later stage is compared against.

Records, for the checkout it runs in: the git commit, the SHA-256 and size of the shipped ROM / cold image / loading image / bitstream in dist/, of an optional
installed-package directory (--package, e.g. a TAU DEV build's `pocket` folder), and the heap-gap snapshots in tools/ram_snapshots/. Writes JSON (default
docs/research/player2/BASELINE.json). A later stage re-runs it and diffs: `--check FILE` exits 1 if a pinned file changed.
"""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def gather(base, label, out):
    for p in sorted(base.rglob("*")):
        if p.is_file() and p.suffix in (".rom", ".bin", ".rbf_r", ".rbf", ".json", ".tdb") and "node_modules" not in p.parts and p.stat().st_size < 64 * 1024 * 1024:
            out[f"{label}/{p.relative_to(base)}"] = {"sha256": sha(p), "bytes": p.stat().st_size}

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--package"); ap.add_argument("--out", default=str(ROOT / "docs/research/player2/BASELINE.json")); ap.add_argument("--check")
    a = ap.parse_args()
    files = {}
    gather(ROOT / "dist", "dist", files)
    if a.package: gather(Path(a.package), "package", files)
    snaps = {}
    for p in sorted((ROOT / "tools/ram_snapshots").glob("*.json")):
        try: snaps[p.stem] = json.loads(p.read_text())
        except ValueError: pass
    doc = {"commit": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip(),
           "dirty": bool(subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout.strip()),
           "files": files, "ram_snapshots": snaps}
    if a.check:
        old = json.loads(Path(a.check).read_text())["files"]
        old = {k: v for k, v in old.items() if a.package or not k.startswith("package/")}   # package files are only compared when --package is given
        bad = [k for k in old if k not in files or files[k]["sha256"] != old[k]["sha256"]]
        for k in bad: print("CHANGED:", k)
        print("baseline", "DIFFERS" if bad else "unchanged", f"({len(old)} files)"); sys.exit(1 if bad else 0)
    Path(a.out).parent.mkdir(parents=True, exist_ok=True); Path(a.out).write_text(json.dumps(doc, indent=1, sort_keys=True) + "\n")
    print(f"pinned {len(files)} files, {len(snaps)} ram snapshots at {doc['commit'][:8]}{' (dirty)' if doc['dirty'] else ''} -> {a.out}")

if __name__ == "__main__": main()
