#!/usr/bin/env python3
"""Owner decision D-M15 (2026-11-07): a WebAssembly module of the firmware's own drawing source must exist for EVERY Player 2 meter, so the lab and Tau Omega run the Pocket's code, not a hand-written twin that can drift.
Every meter manifest with a `p2` block must have tools/meters/preview/meters/<key>_wasm.js (built by tools/meters/preview/wasm/build_wasm.py) and an adapter meters/<key>.js that exposes ready(). The meters that do not have
their module yet are listed in KNOWN_GAPS (the owner's rule is not met for them; each entry is work to do, not an exception): a meter that is missing and not listed fails the test, and so does a listed one that has its
module now (delete the entry). The list only shrinks."""
import json, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
KNOWN_GAPS = set()        # 2026-11-07: every Player 2 meter has its module (Layered Wave, Sound Map, Waterfall Grid, Winamp Bars, Winamp Scope, Master VU, Chladni); `make new-meter` scaffolds one
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
keys = sorted(json.loads(p.read_text())["key"] for p in (ROOT / "meters").glob("*/meter.json") if "p2" in json.loads(p.read_text()))
have = {k for k in keys if (ROOT / f"tools/meters/preview/meters/{k}_wasm.js").is_file()}
missing = set(keys) - have
check("every Player 2 meter (%d) has a WebAssembly module, except the listed gaps (missing and not listed: %s)" % (len(keys), sorted(missing - KNOWN_GAPS)), not (missing - KNOWN_GAPS))
check("a listed gap that now has its module is removed from the list (%s)" % sorted(KNOWN_GAPS & have), not (KNOWN_GAPS & have))
check("every listed gap is a Player 2 meter (%s)" % sorted(KNOWN_GAPS - set(keys)), KNOWN_GAPS <= set(keys))
for k in sorted(have):
    ad = ROOT / f"tools/meters/preview/meters/{k}.js"
    ok = ad.is_file() and __import__("subprocess").run(["node", "-e", "const a=require(process.argv[1]); if(typeof a.ready!=='function'||typeof a.tick!=='function'||typeof a.state!=='function') process.exit(1)", str(ad)], capture_output=True).returncode == 0
    check(f"{k}: the adapter meters/{k}.js exists and exports state, tick and ready (the module compiles asynchronously in a browser)", ok, ad)
    r = __import__("subprocess").run([sys.executable, str(ROOT / "tools/meters/preview/wasm/build_wasm.py"), "--check", k], capture_output=True, text=True)
    check(f"{k}: the committed module is current", r.returncode == 0, r.stdout[-200:])
print("%d of %d Player 2 meters have their module; still to do (D-M15): %s" % (len(have), len(keys), ", ".join(sorted(missing)) or "none"))
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
