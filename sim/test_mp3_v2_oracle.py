#!/usr/bin/env python3
"""MP3 v2 WP-01: third_party/minimp3/ is pristine upstream (every file matches UPSTREAM.sha256 and the hashes in UPSTREAM.md, no extra file besides
UPSTREAM.*, licence is CC0 1.0), the .gitignore keeps only the vectors out, and the host float oracle (tools/mp3_v2/oracle.c) decodes a generated
stream identically on two runs (PCM and float dump byte for byte). Red checks: a one-byte change to a vendored file is detected; a different stream
gives a different decode (the comparison is not vacuous)."""
import hashlib, re, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/mp3_v2"))
import oracle, synth_stream as S
P = ROOT / "third_party/minimp3"
PIN = "48e3a085f0e1a99446b34614e1e7f17f09dc2e82"
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
    have = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file() and not p.name.startswith("UPSTREAM")}
    return ([f for f in want if f in have and have[f] != want[f]], [f for f in want if f not in have], [f for f in have if f not in want])

mis, miss, extra = verify(P)
check("3 upstream files listed", len((P / "UPSTREAM.sha256").read_text().splitlines()) == 3)
check("vendored files match UPSTREAM.sha256", not mis, str(mis))
check("no vendored file missing", not miss, str(miss))
check("no extra file besides UPSTREAM.*", not extra, str(extra))
md = (P / "UPSTREAM.md").read_text()
check("UPSTREAM.md names the pinned commit", PIN in md)
check("UPSTREAM.md carries every SHA-256", all(h in md for h in (l.split("  ")[0] for l in (P / "UPSTREAM.sha256").read_text().splitlines())))
check("LICENSE is CC0 1.0 Universal", (P / "LICENSE").read_text().startswith("CC0 1.0 Universal") and "creativecommons.org/publicdomain/zero/1.0" in (P / "LICENSE").read_text())
check("minimp3.h carries the public-domain dedication", "dedicated all copyright" in (P / "minimp3.h").read_text()[:600])
ig = subprocess.run(["git", "check-ignore", "-q", "third_party/minimp3/minimp3.h"], cwd=ROOT).returncode
check("the vendored header is not git-ignored", ig == 1)
ig = subprocess.run(["git", "check-ignore", "-q", "third_party/minimp3/vectors/x.bit"], cwd=ROOT).returncode
check("the upstream vectors directory stays git-ignored", ig == 0)

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    c = td / "copy"; shutil.copytree(P, c)
    (c / "minimp3.h").write_bytes((c / "minimp3.h").read_bytes() + b"\n")
    mis2, _, _ = verify(c)
    check("red: a changed vendored file is detected", mis2 == ["minimp3.h"], str(mis2))

    exe = oracle.build(td / "build")
    mp3 = td / "gen.mp3"; mp3.write_bytes(S.stream(40, seed=7, mode=1))
    r = []
    for k in (1, 2):
        info = oracle.decode(exe, mp3, pcm=td / f"p{k}.s16", f32=td / f"f{k}.f32")
        r.append((info, (td / f"p{k}.s16").read_bytes(), (td / f"f{k}.f32").read_bytes()))
    check("generated stream decodes 40 frames x 1152 pairs", r[0][0]["decoded"] == 40 and r[0][0]["pairs"] == 40 * 1152 and r[0][0]["channels"] == 2, str(r[0][0]))
    check("oracle: PCM identical on two runs", r[0][1] == r[1][1] and len(r[0][1]) == 40 * 1152 * 4)
    check("oracle: float dump identical on two runs", r[0][2] == r[1][2] and len(r[0][2]) == 40 * 1152 * 8)
    check("oracle: summary identical on two runs", r[0][0] == r[1][0])
    check("oracle: the output is not silence", any(r[0][1]) and r[0][0]["peak"] > 0.05, str(r[0][0]))
    mp3b = td / "gen2.mp3"; mp3b.write_bytes(S.stream(40, seed=8, mode=1))
    oracle.decode(exe, mp3b, pcm=td / "p3.s16")
    check("red: a different stream decodes differently", (td / "p3.s16").read_bytes() != r[0][1])
sys.exit(1 if fails else 0)
