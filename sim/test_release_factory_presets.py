#!/usr/bin/env python3
"""The release zip carries the factory meter presets (B-982; tools/make_release.py add_factory_presets / check_factory_presets): the Player 2 meters read their presets from files under <media root>/tau-presets/<meter id>/.
  1. add_factory_presets writes every file of the factory set (tools/install_factory_presets.py factory()) into Assets/<media platform>/common/tau-presets/ of a package, byte for byte;
  2. a zip made of that package passes check_factory_presets;
  3. mutants of the zip, each refused: a file missing, a byte changed, an extra file under tau-presets/, the files under the wrong platform;
  4. the Preview channel puts them under TAU's common/ (the last platform of the channel), the Stable channel under tau;
  5. the factory set is the one the card installer writes (51 files today: a change of presets.json changes both)."""
import sys, tempfile, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import make_release as mr, tau_layout, install_factory_presets as ifp
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(info)))
    fails += 0 if cond else 1
def refused(names, read, plat="tau"):
    try: mr.check_factory_presets(names, read, plat); return False
    except AssertionError: return True
def zipped(pkg, drop=None, flip=None, extra=None):
    d = Path(tempfile.mkdtemp()); z = d / "r.zip"
    with zipfile.ZipFile(z, "w") as f:
        for p in sorted(pkg.rglob("*")):
            if not p.is_file(): continue
            n = str(p.relative_to(pkg)); b = p.read_bytes()
            if n == drop: continue
            if n == flip: b = bytes([b[0] ^ 1]) + b[1:]
            f.writestr(n, b)
        if extra: f.writestr(extra, b"x")
    return zipfile.ZipFile(z)
with tempfile.TemporaryDirectory() as td:
    pkg = Path(td) / "pkg"; (pkg / "Cores").mkdir(parents=True)
    n = mr.add_factory_presets(pkg, "tau")
    want = ifp.factory()
    check("add_factory_presets writes the whole factory set (%d files) under Assets/tau/common/tau-presets/, byte for byte" % n, n == len(want) and n >= 40 and all((pkg / "Assets/tau/common/tau-presets" / r).read_bytes() == d for r, d in want.items()))
    z = zipped(pkg)
    check("a zip of it passes the check", mr.check_factory_presets(z.namelist(), z.read, "tau") == n)
    first = "Assets/tau/common/tau-presets/" + sorted(want)[0]
    for what, zz in (("a file missing", zipped(pkg, drop=first)), ("a changed byte", zipped(pkg, flip=first)), ("an extra file under tau-presets/", zipped(pkg, extra="Assets/tau/common/tau-presets/99/p9.tpr"))):
        check("mutant: %s is refused" % what, refused(zz.namelist(), zz.read))
    check("mutant: the files under another platform are refused", refused(z.namelist(), z.read, "tau_preview"))
    check("the Stable channel's media platform is tau, the Preview channel's is tau (TAU's common/)", tau_layout.CHANNELS["stable"]["platforms"][-1] == "tau" and tau_layout.CHANNELS["preview"]["platforms"][-1] == "tau")
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
