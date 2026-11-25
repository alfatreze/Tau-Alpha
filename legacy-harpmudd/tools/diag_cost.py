#!/usr/bin/env python3
"""Ablation of the Diagnostic-Build features (fw/diag_features.json): build the Diagnostic Build on the shipped 192 KB link with each converted feature
dropped on its own (plus everything that depends on it), then with all diagnostics (dx) dropped, all features (fx) dropped, and everything dropped, and
record the hot RAM it frees and the cold image it shrinks by. Writes tools/diag_cost.json (checked in, so the numbers are data, not memory).

  python3 tools/diag_cost.py            # measure every converted feature (about 30 s a build)
  python3 tools/diag_cost.py --show     # print the stored table
Every build must link: a feature that cannot be dropped alone is a conversion bug and fails the run."""
import json, os, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
MAN = ROOT / "fw" / "diag_features.json"
OUT = ROOT / "tools" / "diag_cost.json"
ENV = dict(os.environ, RAM_192K="1", CLK66="1", SDRAM_BUSY="1", TAU_BUILD_OUT=str(ROOT / "work" / "diagcost"))

def build(drop):
    env = dict(ENV)
    if drop: env["DIAG_DROP"] = ",".join(drop)
    r = subprocess.run(["bash", "fw/build.sh", "player-library-diagnostic"], cwd=ROOT, capture_output=True, text=True, env=env)
    g = re.search(r"heap gap: (\d+) B", r.stdout); c = re.search(r"= (\d+) B, \d+ symbols", r.stdout)
    if not g or not c:
        sys.exit(f"build with DIAG_DROP={','.join(drop)} failed:\n{r.stdout[-600:]}\n{r.stderr[-1200:]}")
    return int(g.group(1)), int(c.group(1))

def closure(feats, ids):
    out = set(ids)
    while True:
        more = {f["id"] for f in feats.values() if set(f["depends"]) & out} - out
        if not more: return sorted(out)
        out |= more

def show():
    d = json.loads(OUT.read_text())
    print(f"{'drop':34} {'hot free B':>10} {'+hot':>7} {'cold KB':>8} {'-cold KB':>9}")
    for k, v in d["rows"].items():
        print(f"{k:34} {v['hot_free']:>10} {v['hot_gain']:>+7} {v['cold']/1024:>8.1f} {v['cold_gain']/1024:>9.1f}")

def main():
    if "--show" in sys.argv: return show()
    feats = {f["id"]: f for f in json.loads(MAN.read_text())["features"] if f["converted"]}
    base_hot, base_cold = build([])
    rows = {"(nothing dropped)": {"hot_free": base_hot, "hot_gain": 0, "cold": base_cold, "cold_gain": 0}}
    def add(name, drop):
        h, c = build(drop)
        rows[name] = {"hot_free": h, "hot_gain": h - base_hot, "cold": c, "cold_gain": base_cold - c, "dropped": drop}
        print(f"{name:34} hot free {h:6d} ({h - base_hot:+5d})  cold {c / 1024:7.1f} KB ({(base_cold - c) / 1024:+6.1f})", flush=True)
    for fid in feats:
        add(fid + (" (+dependents)" if closure(feats, [fid]) != [fid] else ""), closure(feats, [fid]))
    dx = [i for i, f in feats.items() if f["kind"] == "dx"]; fx = [i for i, f in feats.items() if f["kind"] == "fx"]
    for name in json.loads(MAN.read_text()).get("presets", {}):
        ids = [i for i in feats] if json.loads(MAN.read_text())["presets"][name]["drop"] == "ALL" else json.loads(MAN.read_text())["presets"][name]["drop"]
        add("preset " + name, closure(feats, ids))
    add("ALL diagnostics (dx)", closure(feats, dx)); add("ALL features (fx)", closure(feats, fx)); add("EVERYTHING", sorted(feats))
    OUT.write_text(json.dumps({"link": "192 KB, CLK66, SDRAM_BUSY, player-library-diagnostic", "rows": rows}, indent=2) + "\n")
main()
