#!/usr/bin/env python3
"""tools/meters/preview/tau_host.js: the lab's messages with an embedding page (Tau-Alpha 009, confirmed by Tau Omega 011). Against the real registry (build.data()): a meter by manifest id, the sparse rule (an unnamed parameter takes its default),
unknown keys and non-integers ignored, values clamped to the parameter's range exactly as the firmware's preset reader does (enum 0..n-1, bool 0..1, numbers min..max) and reported in `clamped`, an unknown meter answered with an error,
and `tau-lab-ready` carrying each meter's module SHA-256 as the adapter reports it (equal to the committed module's own record). Needs node. Host-simulated: the page wiring (window messages) is checked in the browser by hand."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/meters/preview"))
import build as lab
if subprocess.run(["node", "--version"], capture_output=True).returncode:
    print("node not found: skipped"); sys.exit(0)
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:300])); fails += 0 if ok else 1
S = lab.data()["schema"]
P2 = [m for m in S["meters"] if (ROOT / "tools/meters/preview/meters" / (m["key"] + "_wasm.js")).exists()]
JS = r'''
const H = require(process.argv[1] + "/tau_host.js"); const S = JSON.parse(require("fs").readFileSync(process.argv[2], "utf8")); const cases = JSON.parse(process.argv[3]);
const T = {}; for (const m of S.meters) { try { T[m.key] = require(process.argv[1] + "/meters/" + m.key + ".js"); } catch (e) {} }
const out = cases.map(c => c.ready ? H.readyPayload(S, T) : H.plan(S, T, c.meter, c.values).reply);
console.log(JSON.stringify(out));
'''
with tempfile.TemporaryDirectory() as td:
    sj = Path(td) / "schema.json"; sj.write_text(json.dumps(S))
    def run(cases):
        r = subprocess.run(["node", "-e", JS, str(ROOT / "tools/meters/preview"), str(sj), json.dumps(cases)], capture_output=True, text=True, cwd=ROOT)
        if r.returncode: print(r.stderr[-600:]); sys.exit(1)
        return json.loads(r.stdout)
    cm = next(m for m in P2 if m["key"] == "chladni"); lw = next(m for m in P2 if m["key"] == "layered_wave")
    num = next(x for x in cm["params"] if x["type"] in ("u8", "u16")); enum = next(x for x in cm["params"] if x["type"] == "enum")
    cases = [
        dict(meter=cm["id"], values={}),
        dict(meter=cm["id"], values={num["key"]: num["max"] + 1000, enum["key"]: 99, "nonsense": 5, "frac": 1}),
        dict(meter=str(cm["id"]), values={num["key"]: -5}),
        dict(meter=cm["id"], values={num["key"]: 1.5, enum["key"]: "2"}),
        dict(meter=9999, values={"a": 1}),
        dict(meter=cm["id"], values=None),
        dict(ready=True),
    ]
    r0, r1, r2, r3, r4, r5, rd = run(cases)
    defaults = {x["key"]: x["default"] for x in cm["params"]}
    check("an empty preset gives every parameter its default (the sparse rule)", r0["values"] == defaults and r0["ignored"] == [] and r0["clamped"] == {} and r0["meter"] == cm["id"] and r0["type"] == "tau-lab-applied", r0)
    check("an out-of-range number is clamped to its maximum and reported", r1["values"][num["key"]] == num["max"] and r1["clamped"][num["key"]] == num["max"], r1)
    check("an enum index past the last option is clamped to the last option", r1["values"][enum["key"]] == len(enum["values"]) - 1 and r1["clamped"][enum["key"]] == len(enum["values"]) - 1, r1)
    check("a key the meter does not have is ignored and listed", "nonsense" in r1["ignored"] and "nonsense" not in r1["values"], r1)
    check("a parameter the message did not name takes its default", all(r1["values"][k] == v for k, v in defaults.items() if k not in (num["key"], enum["key"])), r1)
    check("a negative number is clamped to the minimum (the id may be sent as a string)", r2["values"][num["key"]] == num["min"] and r2["clamped"][num["key"]] == num["min"] and r2["meter"] == str(cm["id"]), r2)
    check("a value that is not an integer is ignored, the default stays", set(r3["ignored"]) == {num["key"], enum["key"]} and r3["values"] == defaults and r3["clamped"] == {}, r3)
    check("an unknown meter is answered with an error and every key ignored", r4["error"] == "unknown meter" and r4["ignored"] == ["a"] and r4["values"] == {}, r4)
    check("a values field that is not an object is treated as empty", r5["values"] == defaults and r5["ignored"] == [], r5)
    ids = {m["id"] for m in rd["meters"]}
    check("tau-lab-ready lists every meter that has a module (the seven Player 2 meters)", {m["key"] for m in P2} <= {m["key"] for m in rd["meters"]} and len(P2) >= 7, [m["key"] for m in rd["meters"]])
    ok = True; bad = []
    for e in rd["meters"]:
        f = ROOT / "tools/meters/preview/meters" / (e["key"] + "_wasm.js")
        if not f.exists(): continue
        txt = f.read_text(); meta = json.loads(txt[txt.index("/*META") + 6:txt.index("META*/")])
        if e["wasm_sha256"] != meta["wasm_sha256"]: ok = False; bad.append((e["key"], e["wasm_sha256"], meta["wasm_sha256"]))
    check("each meter's reported wasm_sha256 equals the committed module's own record", ok and rd["meters"], bad)
    lwe = next(e for e in rd["meters"] if e["key"] == "layered_wave")
    check("Layered Wave also reports the timed build's SHA-256", lwe.get("wasm_sha256_timed") and lwe["wasm_sha256_timed"] != lwe["wasm_sha256"], lwe)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
