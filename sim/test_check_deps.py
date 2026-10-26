#!/usr/bin/env python3
"""tools/check_deps.py on synthetic fw2 trees: a clean tree passes; each kind of break (a forbidden include, a cycle, an include of app, a leaf included, an unknown directory)
fails; and the real fw2/ tree passes."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
def tree(files):
    d = Path(tempfile.mkdtemp())
    for rel, body in files.items():
        p = d / rel; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(body)
    return d
def rc(d): return subprocess.run([sys.executable, str(ROOT / "tools/check_deps.py"), str(d)], capture_output=True, text=True).returncode
base = {"cores/c.h": "", "platform/p.h": '#include "../cores/c.h"\n', "counters/k.h": '#include "../cores/c.h"\n', "aout/a.h": '#include "../platform/p.h"\n#include "../counters/k.h"\n',
        "stream/s.h": '#include "../platform/p.h"\n', "decode/d.h": '#include "../stream/s.h"\n', "transport/t.h": '#include "../decode/d.h"\n#include "../aout/a.h"\n',
        "ui/u.h": '#include "../transport/t.h"\n', "app/m.c": '#include "../ui/u.h"\n#include "../transport/t.h"\n', "diagnostics/x.c": '#include "../aout/a.h"\n'}
check("a clean tree passes", rc(tree(base)) == 0)
b = dict(base); b["aout/a.h"] += '#include "../ui/u.h"\n';              check("aout including ui fails", rc(tree(b)) == 1)
b = dict(base); b["decode/d.h"] += '#include "../app/m.c"\n';           check("anything including app fails", rc(tree(b)) == 1)
b = dict(base); b["platform/p.h"] += '#include "../aout/a.h"\n';        check("platform including aout fails (and it is a cycle)", rc(tree(b)) == 1)
b = dict(base); b["aout/a.h"] += '#include "../diagnostics/x.c"\n';     check("including the diagnostics leaf fails", rc(tree(b)) == 1)
b = dict(base); b["mystery/z.h"] = "";                                   check("an unknown module directory fails", rc(tree(b)) == 1)
b = dict(base); b["stream/s.h"] += '#include "../decode/d.h"\n';        check("a two-module cycle fails", rc(tree(b)) == 1)
b = dict(base); b["ui/meter_x.c"] = '#include "meter.h"\n#include "../gfx/g.h"\n#include "../cores/c.h"\n#include "../platform/mmio.h"\n'; b["ui/meter.h"] = ""; b["gfx/g.h"] = ""; b["platform/mmio.h"] = ""
check("a meter using only gfx, cores, mmio and meter.h passes", rc(tree(b)) == 0)
for bad in ("../transport/t.h", "../aout/a.h", "../platform/p.h", "../ui/u.h", "../stream/s.h"):
    b2 = dict(b); b2["ui/meter_x.c"] += '#include "%s"\n' % bad;  check("a meter including %s fails" % bad, rc(tree(b2)) == 1)
check("the real fw2/ tree passes", subprocess.run([sys.executable, str(ROOT / "tools/check_deps.py")], capture_output=True).returncode == 0)
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
