#!/usr/bin/env python3
"""B-632: the Halcyon Lab page (tools/lab/halcyon_lab.html) is up to date, and its JavaScript computes exactly what the firmware core computes (stage steps, peak boost, attenuation in
eighths) for the eight presets and 60 random settings, by running the page's own script under node against the compiled fw/halcyon_core.h. Skipped when node is not installed."""
import json, os, random, re, shutil, subprocess, sys, tempfile
os.environ["EQ_COEF_BITS"] = "24"
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools" / "lab"))
import halcyon_model as m
import gen_halcyon_lab as lab

fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

html = (ROOT / "tools/lab/halcyon_lab.html")
check("halcyon_lab.html is up to date with gen_halcyon_lab.py", html.exists() and html.read_text() == lab.render())
if not shutil.which("node"):
    print("skip JavaScript comparison (node not installed)")
    sys.exit(1 if fails else 0)
script = re.search(r"<script>(.*)</script>", html.read_text(), re.S).group(1)
script = script.replace("build();sync(true);addEventListener(\"resize\",render);", "")
rnd = random.Random(21)
sets = [dict(p[1]) for p in m.PRESETS] + [{k: (rnd.randint(0, 5) if k == "sibilance" else rnd.randint(-5, 5)) for k in m.MACROS} for _ in range(60)]
JS = script + r"""
const sets = JSON.parse(require('fs').readFileSync(0, 'utf8'));
const out = sets.map(c => { MACROS.forEach(k => state[k] = c[k] || 0); const r = compute(); return r.st.concat([r.eighths]); });
console.log(JSON.stringify(out));
"""
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d / "t.js").write_text(JS)
    js = json.loads(subprocess.run(["node", str(d / "t.js")], input=json.dumps(sets), capture_output=True, text=True, check=True).stdout)
    exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-I", str(ROOT / "fw"), "-o", str(exe), str(ROOT / "sim/halcyon_core_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); sys.exit(1)
    inp = "\n".join(" ".join(str(c.get(k, 0)) for k in m.MACROS) for c in sets)
    cout = subprocess.run([str(exe)], input=inp, capture_output=True, text=True, check=True).stdout.splitlines()
    bad = 0
    for jv, line in zip(js, cout):
        v = list(map(int, line.split()))
        if jv != v[:6] + [v[36]]:
            bad += 1
    check("page script equals the firmware core (steps and attenuation) for %d settings" % len(sets), bad == 0, "(%d differ)" % bad)
sys.exit(1 if fails else 0)
