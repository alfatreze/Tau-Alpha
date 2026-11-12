#!/usr/bin/env python3
"""How many parameters of each shipped meter a saved tuning holds, with the layout-1 record (entries from bit 24, 100 bits) and the layout-2 record (entries from bit 39, 85 bits): fw2/cores/tune_core.h
(tune_nbits: the value width of a parameter; each entry costs 6 + that; entries are taken in index order, one that does not fit is skipped, at most 15). Real manifests (meters/*/meter.json).
  python3 tools/player2/tune_capacity.py     prints the markdown table that docs/features/PERSISTENCE_LAYOUT.md and B-1049 carry"""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_manifest as mm, mpreset

def nbits(mx):
    n = 1
    while n < 16 and (mx >> n): n += 1
    return n

def maxes(d):
    """the largest value of each parameter as the firmware's tune_max_of sees it: colour 65535, on/off 1, else the descriptor's max"""
    return [65535 if r["type"] == 3 else 1 if r["type"] == 2 else r["max"] for r in mpreset.md.param_rows(d)]

def fit(idx, mx, head, cap=124):
    pos, n = head, 0
    for i in idx:
        if n >= 15: break
        c = 6 + nbits(mx[i])
        if pos + c > cap: continue
        pos += c; n += 1
    return n

def guaranteed(mx, head):
    """the largest k such that ANY k parameters fit (the k most expensive ones are the worst case)"""
    cost = sorted((6 + nbits(m) for m in mx), reverse=True); pos = head; k = 0
    for c in cost:
        if pos + c > 124 or k >= 15: break
        pos += c; k += 1
    return k

def rows():
    out = []
    for p in sorted((ROOT / "meters").glob("*/meter.json")):
        d = mm.load(p)
        if "p2" not in d or d["p2"].get("slot") is None: continue
        mx = maxes(d); idx = list(range(len(mx))); keys = [r["key"] for r in mpreset.md.param_rows(d)]
        dflt = {q["key"]: q.get("default", 0) for q in d["params"]}
        worst = [0, 0]; lost = []
        for pr in d.get("presets", []):                  # a factory preset as the change from the defaults: the entries it would need
            ch = [i for i, k in enumerate(keys) if pr["values"].get(k, dflt.get(k)) != dflt.get(k)]
            a, b = fit(ch, mx, 24), fit(ch, mx, 39)
            if a != b: lost.append((pr["name"], len(ch), a, b))
            worst = [max(worst[0], len(ch)), max(worst[1], b)]
        out.append((d["p2"]["slot"], d["key"], len(mx), fit(idx, mx, 24), fit(idx, mx, 39), guaranteed(mx, 24), guaranteed(mx, 39), worst[0], lost))
    return sorted(out)

def table():
    t = ["| slot | meter | parameters | all changed: stored before (100 bits) | after (85 bits) | any k always fit: before | after | most parameters a factory preset changes | presets that lose a parameter |", "|---|---|---|---|---|---|---|---|---|"]
    for s, k, n, a, b, ga, gb, w, lost in rows():
        t.append("| %d | %s | %d | %d | %d | %d | %d | %d | %s |" % (s, k, n, a, b, ga, gb, w, "none" if not lost else "; ".join("%s (%d changed: %d -> %d)" % l for l in lost)))
    return "\n".join(t)

if __name__ == "__main__": print(table())
