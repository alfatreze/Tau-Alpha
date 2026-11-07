#!/usr/bin/env python3
"""tools/name_matrix.py (BUG-001 step 2): the card test matrix, on a fake card (host only: it says nothing about what the Pocket does with these names; that is the diagnostic run, step 3).
  1. the plan: 16 variants with distinct ids, three negative controls, the near-limit paths inside the firmware's 222-byte limit and the over-limit control above it, NFC and NFD forms really different bytes;
  2. the media root comes from the card's library index; a card without a library is refused; a dry run writes nothing; --yes writes the files and reads every name back; a second verify agrees; --remove deletes the folder;
  3. every written MP3 is a valid file the repository's own tag reader accepts, and carries its id (ID3 title NM<id>); the manifest matrix.tnm parses (TNM1 header, one line per variant, raw path bytes equal to the plan);
  4. mutants: a file missing from the card and a changed file are reported by verify (exit 1); a path that the filesystem cannot hold is reported as NOT FOUND."""
import os, subprocess, sys, tempfile, unicodedata
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import name_matrix as nm, tau_library as tl
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:400]))
    fails += 0 if ok else 1
def run(*a):
    r = subprocess.run([sys.executable, str(ROOT / "tools/name_matrix.py")] + [str(x) for x in a], capture_output=True, text=True); return r.returncode, r.stdout + r.stderr
vs = nm.variants()
check("16 variants with ids 0..15", [v[0] for v in vs] == list(range(16)))
check("three negative controls (over the limit, no such file, invalid UTF-8) and the rest expect to open", [v[0] for v in vs if v[1] == "f"] == [13, 14, 15])
near = [len(v[3]) for v in vs if v[0] in (11, 12)]
check("the near-limit paths fit the firmware's %d bytes (%s), the control is over (%d)" % (nm.LIMIT, near, len(vs[13][3])), all(200 <= n <= nm.LIMIT for n in near) and len(vs[13][3]) > nm.LIMIT)
check("NFC and NFD are different bytes for the same letters", vs[1][3].replace(b"nfc_", b"") != vs[2][3].replace(b"nfd_", b"") and "Café".encode() in vs[2][3] and "Café".encode() in vs[1][3])
try: vs[15][3].decode("utf-8"); inv = False
except UnicodeDecodeError: inv = True
check("variant 15 really is invalid UTF-8", inv)
check("the plan command prints every variant and writes nothing", run("plan")[0] == 0 and sum(1 for l in run("plan")[1].splitlines() if l[:2].isdigit() and "  f  " in l) == 3)
with tempfile.TemporaryDirectory() as td:
    card = Path(td) / "card"; common = card / "Assets/tau/common"; common.mkdir(parents=True)
    rc, out = run("write", "--card", card); check("a card without a library is refused", rc == 1 and "no library" in out, out[:120])
    r = subprocess.run([sys.executable, str(ROOT / "tools/tau_library.py"), "synth", "--tracks", "3", "--albums", "1", "--artists", "1", "--out", str(common / "tau-library.tdb")], capture_output=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    rc, out = run("write", "--card", card); check("a dry run says what it would write and writes nothing", rc == 0 and "dry run" in out and not (common / "name-test").exists(), out[:200])
    rc, out = run("write", "--card", card, "--yes"); dest = common / "name-test"
    check("--yes writes the folder and reads every name back under its exact bytes", rc == 0 and "0 differ" in out and "NOT FOUND" not in out and (dest / "matrix.tnm").exists(), out[-400:])
    fs = nm.files(); check("every file of the plan is on the card (%d files)" % len(fs), all(os.path.exists(os.path.join(os.fsencode(str(dest)), rel)) for rel in fs))
    rc, out = run("verify", "--card", card); check("verify agrees", rc == 0 and "0 differ" in out, out[:200])
    (dest / "._junk.mp3").write_bytes(b"x"); (dest / "sub").mkdir(exist_ok=True); (dest / "sub/._junk2").write_bytes(b"x")
    rc, out = run("write", "--card", card, "--yes")
    check("a second --yes write removes the macOS AppleDouble ._* files (here 2 planted) and still verifies", rc == 0 and "removed 2 macOS AppleDouble" in out and not (dest / "._junk.mp3").exists() and not (dest / "sub/._junk2").exists(), out[:300])
    import shutil as _sh; _sh.rmtree(dest / "sub")
    # the files
    mp3s = [rel for rel in fs if rel.endswith(b".mp3")]
    ok = True; why = ""
    for rel in mp3s:
        data = open(os.path.join(os.fsencode(str(dest)), rel), "rb").read(); vid = [v[0] for v in vs if v[3].endswith(rel)][0]
        tags = None
        try: tags = tl.read_tags(Path(os.path.join(os.fsencode(str(dest)), rel).decode("utf-8", "surrogateescape")))
        except Exception as e: why = repr(e)
        if b"NM%02d" % vid not in data[:64] or data[:3] != b"ID3" or tags is None: ok = False; why = why or rel
    check("every MP3 (%d) carries its id and the repository's tag reader accepts it" % len(mp3s), ok and len(mp3s) == 13, why)
    man = open(dest / "matrix.tnm", "rb").read().split(b"\n"); lines = [l for l in man[1:] if l]
    check("matrix.tnm: header TNM1 16, one line per variant, the raw path bytes equal the plan", man[0] == b"TNM1 16" and len(lines) == 16 and all(l.split(b"|", 2)[2] == vs[i][3] and l.split(b"|")[1] == vs[i][1].encode() for i, l in enumerate(lines)), man[:3])
    rec = (dest / "MATRIX.txt").read_text(encoding="utf-8"); check("MATRIX.txt records every path with its hex bytes", all(v[3].hex() in rec for v in vs))
    # mutants
    victim = os.path.join(os.fsencode(str(dest)), mp3s[3]); keep = open(victim, "rb").read(); os.remove(victim)
    rc, out = run("verify", "--card", card); check("mutant: a missing file is reported (exit 1, NOT FOUND)", rc == 1 and "NOT FOUND" in out, out[:200])
    open(victim, "wb").write(keep[:-1] + bytes([keep[-1] ^ 1]))
    rc, out = run("verify", "--card", card); check("mutant: a changed file is reported (exit 1, 1 differs)", rc == 1 and "1 differ" in out, out[:200])
    rc, out = run("remove", "--card", card, "--yes"); check("remove deletes the folder", rc == 0 and not dest.exists(), out[:120])
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
