#!/usr/bin/env python3
"""fw/diag_features.json and tools/gen_diag_features.py: the register is complete and consistent, the generated header matches, presets and the build flags
behave, and bad requests stop the build instead of silently doing nothing. Red cases are the ones the generator must refuse."""
import json, re, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info and not ok else ""))
    if not ok: fails += 1
def gen(*a):
    r = subprocess.run([sys.executable, str(ROOT / "tools/gen_diag_features.py"), *a], capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()

d = json.loads((ROOT / "fw/diag_features.json").read_text())
feats = {f["id"]: f for f in d["features"]}
classes = set(d["_classes"])
check("ids are unique", len(feats) == len(d["features"]))
check("every feature has kind, class, influences and safe_to_drop", all(f["kind"] in ("dx", "fx") and f.get("class") in classes and f.get("influences") and f.get("safe_to_drop") for f in feats.values()))
check("every feature is converted (nothing left hanging off TAU_DIAGNOSTIC alone)", all(f["converted"] for f in feats.values()))
check("dependencies name real features", all(x in feats for f in feats.values() for x in f["depends"]))
check("generated header is up to date", gen("--check")[0] == 0)
hdr = (ROOT / "fw/diag_features.h").read_text()
check("every converted feature has its macro defaulting to TAU_DIAGNOSTIC", all(re.search(r"#define TAU_%s_%s TAU_DIAGNOSTIC" % (f["kind"].upper(), i.upper()), hdr) for i, f in feats.items()))
# every macro the firmware tests is a registered one (a typo would silently compile the code out)
used = set()
for p in list((ROOT / "fw").glob("*.inc")) + list((ROOT / "fw").glob("*.c")) + list((ROOT / "fw").glob("*.h")):
    used |= set(re.findall(r"\bTAU_(?:DX|FX)_[A-Z0-9_]+\b", p.read_text()))
reg = {"TAU_%s_%s" % (f["kind"].upper(), i.upper()) for i, f in feats.items()}
check("the firmware only tests registered feature macros", used <= reg, str(sorted(used - reg)))
check("every registered macro is used somewhere in the firmware", reg <= used, str(sorted(reg - used)))
for name, pr in d["presets"].items():
    ids = list(feats) if pr["drop"] == "ALL" else pr["drop"]
    check(f"preset {name}: names real features", all(i in feats for i in ids))
    rc, out = gen("--cflags", "", "", name)
    check(f"preset {name}: produces flags", rc == 0 and out.count("-D") >= len(ids))
flags = gen("--cflags", "", "", "slim")[1]
check("slim does not switch off cymo_toggle, check or load_stats", all(m not in flags for m in ("TAU_FX_CYMO_TOGGLE", "TAU_DX_CHECK=", "TAU_DX_LOAD_STATS")))
# refusals
check("dropping load_stats alone is refused (check needs it)", gen("--cflags", "load_stats")[0] != 0)
check("an unknown id is refused", gen("--cflags", "nope")[0] != 0)
check("turning a diagnostic ON is refused", gen("--cflags", "", "stress")[0] != 0)
check("graduating a feature is allowed", gen("--cflags", "", "meter_experimental")[1] == "-DTAU_FX_METER_EXPERIMENTAL=1")
check("dropping a feature gives its macro", gen("--cflags", "stress")[1] == "-DTAU_DX_STRESS=0")
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
