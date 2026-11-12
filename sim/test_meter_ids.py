#!/usr/bin/env python3
"""The identity of every shipped Player 2 meter (owner decision D-M23, 2026-11-12): meters/<key>/meter.json carries "id", 128 random bits as 32 lower-case hex digits, made once and NEVER changed (the selected meter, the tunings and the preset folder name are keyed by it, docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2, 5 and 7a). The table below is the accepted one: a change here is a deliberate break of every saved setting."""
import json, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_manifest as mm
ACCEPTED = {"winamp_bars": "7dddfe63121e904843088c075cc28ce9", "winamp_scope": "143acb80cbcd1b08b84c2661eeab3414", "chladni": "7471cc214987407c0b03917f27923179",
            "layered_wave": "5812803445b81ffae735527099ef6793", "vu_master": "0d8c03d3a2770c1aac2dc6abbc222afd", "waterfall_grid": "17e607764b83b9372452eda124ae763f",
            "sound_map": "0c82d9f07f73f25d90ca54c8d39173ce"}
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail)); fails += 0 if ok else 1
ms = [mm.load(p) for p in sorted((ROOT / "meters").glob("*/meter.json"))]
shipped = {m["key"]: m for m in ms if "p2" in m and m["p2"].get("slot") is not None}
check("the seven shipped Player 2 meters are the accepted seven", set(shipped) == set(ACCEPTED), str(sorted(shipped)))
for k, i in ACCEPTED.items():
    check(f"{k}: the id is the accepted one", shipped.get(k, {}).get("id") == i, str(shipped.get(k, {}).get("id")))
ids = [m["id"] for m in shipped.values() if "id" in m]
check("every id is 32 lower-case hex digits", all(re.fullmatch(r"[0-9a-f]{32}", i) for i in ids))
check("every id_hash (first 8 digits) is unique", len({i[:8] for i in ids}) == len(ids))
check("every id is unique", len(set(ids)) == len(ids))
check("a retired or unlisted manifest carries no id", all("id" not in m for m in ms if m["key"] not in shipped))
src = (ROOT / "tools/player2/new_meter.py").read_text()
check("the scaffolder gives a new meter a fresh random id", "secrets.token_hex(16)" in src and '"id": mid' in src)
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
