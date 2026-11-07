#!/usr/bin/env python3
"""tools/install_factory_presets.py (B-967) on a fake card: the media root comes from the card's library index; a dry run writes nothing; --yes writes every factory file under <root>/tau-presets/<meter id>/ and verifies it;
a second run finds everything identical; a file that differs (a newer set from Omega) is refused unless --force; --remove deletes the folder; a card without a library is refused. Host-simulated only."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)))
    fails += 0 if ok else 1
def run(*a):
    r = subprocess.run([sys.executable, str(ROOT / "tools/install_factory_presets.py")] + [str(x) for x in a], capture_output=True, text=True); return r.returncode, r.stdout + r.stderr
with tempfile.TemporaryDirectory() as td:
    card = Path(td) / "card"; common = card / "Assets/tau/common"; common.mkdir(parents=True)
    other = card / "Assets/aaa_other/common"; other.mkdir(parents=True)                      # another core's library must not be picked: only the Tau platforms count
    subprocess.run([sys.executable, str(ROOT / "tools/tau_library.py"), "synth", "--tracks", "2", "--albums", "1", "--artists", "1", "--out", str(other / "tau-library.tdb")], capture_output=True)
    rc, out = run("--card", card); check("a card without a library is refused", rc == 1 and "no library" in out, out[:120])
    r = subprocess.run([sys.executable, str(ROOT / "tools/tau_library.py"), "synth", "--tracks", "3", "--albums", "1", "--artists", "1", "--out", str(common / "tau-library.tdb")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    rc, out = run("--card", card); check("a dry run says what it would do and writes nothing", rc == 0 and "dry run" in out and not (common / "tau-presets").exists(), out[:200])
    rc, out = run("--card", card, "--yes"); n = len(list((common / "tau-presets").rglob("*.t*")))
    check("--yes writes the factory files of every Player 2 meter under <root>/tau-presets/<meter id>/ and verifies them (%d files)" % n, rc == 0 and "written and verified" in out and n >= 40 and (common / "tau-presets/16/index.tpi").exists() and (common / "tau-presets/16/p0.tpr").exists(), out[-200:])
    rc, out = run("--card", card, "--yes"); check("a second run finds everything identical", rc == 0 and "0 new" in out and "0 there and different" in out, out[:200])
    f = common / "tau-presets/16/p3.tpr"; b = f.read_bytes(); f.write_bytes(b[:-1] + bytes([b[-1] ^ 1]))
    rc, out = run("--card", card, "--yes"); check("a file that differs (a newer set) is refused without --force", rc == 1 and "refusing" in out, out[:200])
    rc, out = run("--card", card, "--yes", "--force"); check("--force overwrites it", rc == 0 and "written and verified" in out, out[-120:])
    for st in (common / "tau-presets/16/._index.tpi", common / "tau-presets/16/._p0.tpr", common / "tau-presets/._16"): st.write_bytes(b"\x00\x05\x16\x07")          # AppleDouble stubs macOS writes beside copied files (Omega found 58)
    keep = common / "tau-presets/16/mine.txt"; keep.write_bytes(b"x")
    rc, out = run("--card", card, "--yes")
    check("--yes sweeps the AppleDouble ._* stubs inside tau-presets/ (and nothing else there)", rc == 0 and not list((common / "tau-presets").rglob("._*")) and keep.exists(), (out[-200:], list((common / "tau-presets").rglob("._*"))))
    keep.unlink()
    rc, out = run("--card", card, "--yes", "--remove"); check("--remove deletes the folder", rc == 0 and not (common / "tau-presets").exists(), out[:120])
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
