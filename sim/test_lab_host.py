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

# ---- contract 1 (Tau Omega message 014): views, options, cost windows, the contract number ----
import re
D = lab.data()
check("lab-data carries the contract number", D["contract"] == 1 and rd.get("contract") is None, D.get("contract"))   # the ready payload carries it only when the page passes it
V = D["views"]; fs = (ROOT / "fw2/ui/fullscreen_view.c").read_text(); rg = (ROOT / "fw2/ui/regions.h").read_text()
def const(txt, name): return int(re.search(r"#define\s+%s\s+(\d+)u?" % name, txt).group(1))
check("the full view is the firmware's fullscreen figure (400 x 323 at 0,0)", V["rects"]["full"] == {"x": 0, "y": 0, "w": const(fs, "FS_W"), "h": const(fs, "FS_FIG_H")}, V["rects"])
sm = V["rects"]["small"]
check("the small view has the firmware's Now Playing meter size and x", (sm["x"], sm["w"], sm["h"]) == (const(rg, "NP_METER_X"), const(rg, "NP_METER_W"), const(rg, "NP_METER_Y1") - const(rg, "NP_METER_Y0")), sm)
sel = {str(m["id"]) for m in S["meters"] if m.get("selectable") and not m.get("retired")}
check("every selectable meter has both views, a retired one neither", {k for k, v in V["meters"].items() if v["small"] and v["full"]} == sel and not any(v["small"] or v["full"] for k, v in V["meters"].items() if k not in sel), V["meters"])
check("the views do not touch schema.meters (Omega compares that registry meter by meter)", all("views" not in m for m in S["meters"]))
JS2 = r'''
const H = require(process.argv[1] + "/tau_host.js"); const D = JSON.parse(require("fs").readFileSync(process.argv[2], "utf8")); const S = D.schema;
const defs = H.optionDefs(S, D); const id = (k) => S.meters.find((m) => m.key === k).id;
const out = {};
out.defs = defs.map((d) => ({ id: d.id, type: d.type, applies_to: d.applies_to, n: d.choices ? d.choices.length : null, min: d.min, max: d.max, dom: !!d.dom }));
const vals = { theme: 0, mode: 0, accent: 1, src: 0, fullfig: false, barready: true, timedg: false, statsok: true, corr: 120 };
out.wire = {}; for (const k of ["winamp_bars", "layered_wave", "sound_map", "chladni"]) out.wire[k] = H.wireOptions(defs, vals, id(k)).map((o) => o.id);
out.wireLeak = H.wireOptions(defs, vals, id("sound_map")).some((o) => "dom" in o);
const sm = id("sound_map");
out.chk = { ok: H.checkOption(defs, sm, "corr", 100), hi: H.checkOption(defs, sm, "corr", 999), lo: H.checkOption(defs, sm, "corr", -999), frac: H.checkOption(defs, sm, "corr", 1.5), str: H.checkOption(defs, sm, "corr", "3"),
  enumHi: H.checkOption(defs, sm, "theme", 999), enumOk: H.checkOption(defs, sm, "mode", 1), boolT: H.checkOption(defs, sm, "statsok", true), boolI: H.checkOption(defs, sm, "statsok", 0), boolBad: H.checkOption(defs, sm, "statsok", 2),
  unknown: H.checkOption(defs, sm, "zoom", 1), wrongMeter: H.checkOption(defs, id("chladni"), "corr", 1), strId: H.checkOption(defs, String(sm), "corr", 1), srcHi: H.checkOption(defs, sm, "src", 7) };
const B = [40, 100, 400];
const c = []; for (let i = 0; i < 150; i++) c.push(i < 100 ? 10 : (i === 149 ? 450 : 20));
out.w = H.costWindows(c, 2, B); out.w1 = H.costWindows(c, 1, B); out.wnull = H.costWindows([], null, B); out.wshort = H.costWindows([5, 7], 0, B);
out.cc = [H.costClass(null), H.costClass({}), H.costClass({cost_class: null}), H.costClass({cost_class: 0}), H.costClass({cost_class: 1}), H.costClass({cost_class: 2})];
out.old = H.costWindows(Array(130).fill(500).concat(Array(120).fill(10)), 2, B);   // a spike older than the window no longer counts
console.log(JSON.stringify(out));
'''
with tempfile.TemporaryDirectory() as td:
    dj = Path(td) / "data.json"; dj.write_text(json.dumps(D))
    r = subprocess.run(["node", "-e", JS2, str(ROOT / "tools/meters/preview"), str(dj)], capture_output=True, text=True, cwd=ROOT)
    if r.returncode: print(r.stderr[-600:]); sys.exit(1)
    o = json.loads(r.stdout)
dd = {d["id"]: d for d in o["defs"]}
check("the preview controls are exactly the nine documented options", list(dd) == ["theme", "mode", "accent", "src", "fullfig", "barready", "timedg", "statsok", "corr"], list(dd))
check("source offers only the three that need no file or microphone", dd["src"]["n"] == 3 and dd["src"]["dom"], dd["src"])
check("theme and accent list the registry's own names", dd["theme"]["n"] == len(D["themes"]) and dd["accent"]["n"] == len(D["palette"]), dd)
check("each option applies to the right meters (Hard-coded by Omega until now)", o["wire"]["winamp_bars"] == ["theme", "mode", "accent", "src", "fullfig", "barready"] and o["wire"]["layered_wave"] == ["theme", "mode", "accent", "src", "fullfig", "timedg"]
      and o["wire"]["sound_map"] == ["theme", "mode", "accent", "src", "fullfig", "statsok", "corr"] and o["wire"]["chladni"] == ["theme", "mode", "accent", "src", "fullfig"], o["wire"])
check("the page's own control values (dom) are not sent", not o["wireLeak"])
k = o["chk"]
check("an in-range range value is accepted as is", k["ok"] == {"ok": True, "value": 100, "clamped": False}, k["ok"])
check("a range value out of range is clamped and said so", k["hi"] == {"ok": True, "value": 256, "clamped": True} and k["lo"] == {"ok": True, "value": -256, "clamped": True}, k)
check("a fraction or a string is refused as not a number", k["frac"]["error"] == "not a number" and k["str"]["error"] == "not a number", k)
check("an enum past the last choice is clamped", k["enumHi"]["clamped"] and k["enumHi"]["value"] == len(D["themes"]) - 1 and k["srcHi"]["value"] == 2, k)
check("a bool takes true/false/0/1 and refuses 2", k["boolT"]["value"] == 1 and k["boolI"]["value"] == 0 and k["boolBad"]["error"] == "not a number", k)
check("an unknown option and an option of another meter are refused", k["unknown"]["error"] == "unknown option" and k["wrongMeter"]["error"] == "not for this meter", k)
check("the meter may be given as a string", k["strId"]["ok"], k["strId"])
w = o["w"]
check("cost window: 120-frame verdict window and 30-frame recent window", w["window"]["n"] == 120 and w["recent"]["n"] == 30 and w["frame"] == 450, w)
check("the verdict is the worst of the last 120 frames against the class budget", w["window"]["max"] == 450 and w["budget"] == 400 and w["over"] is True and o["w1"]["budget"] == 100 and o["w1"]["over"] is True, w)
check("the means are of their own window", w["window"]["mean"] == round(sum(([10] * 100 + [20] * 49 + [450])[-120:]) / 120, 1) and w["recent"]["mean"] == round((20 * 29 + 450) / 30, 1), w)
check("a spike older than the window no longer counts", o["old"]["over"] is False and o["old"]["window"]["max"] == 10, o["old"])
check("an empty or short history is safe; no class means the largest budget", o["wnull"]["window"]["n"] == 0 and o["wnull"]["budget"] == 400 and o["wnull"]["class"] == 2 and o["wshort"]["window"]["n"] == 2 and o["wshort"]["over"] is False, o)
check("a meter with no cost class counts as class 2 (the same number in tau-lab-cost and tau-lab-info; Omega 015 F-1)", o["cc"] == [2, 2, 2, 0, 1, 2], o["cc"])
html0 = (ROOT / "tools/meters/preview/index.html").read_text()
check("tau-lab-info and tau-lab-cost both take the class from TauHost.costClass", "class:TauHost.costClass(m)" in html0 and "TauHost.costWindows(costs,TauHost.costClass(mm)" in html0 and "class:m.cost_class" not in html0)
# the page wires every message of the contract (checked in the browser by hand; here the source is checked for each type and for the ids the contract documents as stable)
html = (ROOT / "tools/meters/preview/index.html").read_text()
for t in ("tau-lab-set", "tau-lab-embed", "tau-lab-option", "tau-lab-info-get", "tau-lab-options", "tau-lab-size", "tau-lab-cost", "tau-lab-info", "tau-lab-option-applied"):
    check("index.html handles or sends " + t, ("'" + t + "'") in html, t)
for eid in ("theme", "mode", "accent", "src", "fullfig", "barready", "timedg", "statsok", "corr", "cv", "meter", "infod"):
    check("the documented element id #" + eid + " is in the page", ('id="%s"' % eid) in html, eid)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
