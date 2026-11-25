#!/usr/bin/env python3
"""B-1168: third_party/libhelix-mp3/ is pristine upstream (every file matches UPSTREAM.sha256, no extra files besides Tau's three), tau.patch applies,
and the build tree tools/helix_tree.py assembles has the patched content. Red check: a one-byte change to a pristine file must be detected."""
import hashlib, subprocess, sys, tempfile, shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import helix_tree as HT
P = HT.PRISTINE
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1

def verify(root):
    """-> (mismatched, missing, extra) against UPSTREAM.sha256"""
    want = {}
    for line in (P / "UPSTREAM.sha256").read_text().splitlines():
        h, f = line.split("  ", 1); want[f] = h
    have = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob("*") if p.is_file()}
    for t in HT.SKIP: have.pop(t, None)
    return ([f for f in want if f in have and have[f] != want[f]], [f for f in want if f not in have], [f for f in have if f not in want])

mis, miss, extra = verify(P)
check("70 upstream files listed", len((P / "UPSTREAM.sha256").read_text().splitlines()) == 70)
check("vendored files match upstream SHA-256", not mis, str(mis))
check("no upstream file missing", not miss, str(miss))
check("no extra file in the pristine directory", not extra, str(extra))
check("mp3_profile.h is a Tau file outside the pristine directory", not (P / "pub/mp3_profile.h").exists() and (HT.TAU_FILES / "pub/mp3_profile.h").is_file())

tree = HT.helix_tree()
check("build tree has mp3_profile.h", (tree / "pub/mp3_profile.h").is_file())
for f, marker in (("mp3dec.c", "MP3_PROFILE"), ("real/imdct.c", "MPROF_"), ("real/dct32.c", "TAU_POLY_LOG"), ("real/subband.c", "TAU_POLY_FW"), ("real/coder.h", "TAU_POLY_FW")):
    check("patched " + f + " carries " + marker, marker in (tree / f).read_text())
for f in ("real/polyphase.c", "real/trigtabs.c", "real/bitstream.c"):
    check("untouched " + f + " is the pristine file", (tree / f).read_bytes() == (P / f).read_bytes())
# the patch touches exactly the five files it is documented to touch
touched = sorted(l[6:] for l in (P / "tau.patch").read_text().splitlines() if l.startswith("+++ b/"))
check("tau.patch touches exactly the 5 documented files", touched == ["mp3dec.c", "real/coder.h", "real/dct32.c", "real/imdct.c", "real/subband.c"], str(touched))

# red: a modified pristine file is caught, and a patch that no longer applies stops the build
with tempfile.TemporaryDirectory() as td:
    c = Path(td) / "copy"; shutil.copytree(P, c)
    (c / "real/polyphase.c").write_bytes((c / "real/polyphase.c").read_bytes() + b"\n")
    mis2, _, _ = verify(c)
    check("red: a changed pristine file is detected", mis2 == ["real/polyphase.c"], str(mis2))
    (c / "real/dct32.c").write_bytes(b"/* different */\n")
    r = subprocess.run(["patch", "-p1", "-s", "--dry-run", "-d", str(c), "-i", str(P / "tau.patch")], capture_output=True, text=True)
    check("red: tau.patch no longer applies to a changed base", r.returncode != 0)
sys.exit(1 if fails else 0)
