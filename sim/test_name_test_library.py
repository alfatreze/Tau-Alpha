#!/usr/bin/env python3
"""tools/make_name_test_library.py: one UTF-8-tagged MP3 per creatable case of the name corpus, named with the original file name; silent by default, or the audio of a given MP3 (its tags dropped, optionally cut); refuses a non-empty target;
and the tree it makes goes through tools/sync_media.py --library into a verified index with a sidecar that binds to it."""
import json, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "sim"))
import name_corpus as C, make_name_test_library as G, tau_library as L, name_sidecar as NS
fails = 0
def check(n, ok, i=""):
    global fails
    print(("ok   " if ok else "FAIL ") + n + ("" if ok else "  " + str(i)[:300])); fails += 0 if ok else 1
def run(*a): return subprocess.run([sys.executable, str(ROOT / "tools/make_name_test_library.py"), *a], capture_output=True, text=True)
with tempfile.TemporaryDirectory() as td:
    td = Path(td); out = td / "lib"
    r = run("--out", str(out), "--seconds", "2")
    files = sorted(out.rglob("*.mp3"))
    want = len([c for c in C.CASES if c["id"] not in G.SKIP])
    check(f"it makes one file per creatable corpus case ({want})", r.returncode == 0 and len(files) == want, (r.stdout + r.stderr, len(files)))
    nfc = {__import__("unicodedata").normalize("NFC", p.name) for p in files}
    check("the files carry the ORIGINAL names (accents, Cyrillic, CJK, emoji)", any("Привет" in n for n in nfc) and any("東京" in n for n in nfc) and any("🎉" in n for n in nfc) and any("ß" in n for n in nfc), sorted(nfc)[:5])
    d = files[0].read_bytes()
    check("each file starts with an ID3v2.4 tag and then MPEG frames", d[:4] == b"ID3\x04" and b"\xff\xfb" in d[10:], d[:8])
    src = td / "src.mp3"; src.write_bytes(b"ID3\x03\x00\x00" + bytes([0, 0, 0, 5]) + b"xxxxx" + (b"\xff\xfb\x90\x64" + b"\x01" * 413) * 50)
    out2 = td / "lib2"; r = run("--out", str(out2), "--audio", str(src))
    f2 = next(out2.rglob("*.mp3")).read_bytes()
    check("with --audio the given MP3's frames are used (its own tag dropped) in full", (b"\xff\xfb\x90\x64" + b"\x01" * 413) * 50 in f2 and b"xxxxx" not in f2, r.stdout + r.stderr)
    out3 = td / "lib3"; run("--out", str(out3), "--audio", str(src), "--seconds", "0.01")
    check("--seconds cuts the audio (16,000 bytes a second)", len(next(out3.rglob("*.mp3")).read_bytes()) < len(f2) - 20000)
    check("a non-empty target is refused", run("--out", str(out)).returncode != 0)
    card = td / "card"; (card / "Cores/alfatreze.TAU X").mkdir(parents=True); (card / "Cores/alfatreze.TAU X/core.json").write_text(json.dumps({"core": {"metadata": {"platform_ids": ["tau"]}}}))
    (card / "Assets/tau/common").mkdir(parents=True)
    s = subprocess.run([sys.executable, str(ROOT / "tools/sync_media.py"), str(out), "--card", str(card), "--core", "alfatreze.TAU X", "--library", "--no-art-variants"], capture_output=True, text=True)
    c = card / "Assets/tau/common"
    ok = s.returncode == 0 and (c / "tau-library.tdb").exists()
    check("the tree goes through sync_media --library", ok, (s.stdout + s.stderr)[-400:])
    if ok:
        data = (c / "tau-library.tdb").read_bytes(); ix = L.parse(data)
        check("the index verifies and the sidecar binds to it", L.verify(data, c) == [] and json.loads((c / "tau-names.json").read_text())["index_build_id"] == ix.build_id and
              NS.parse_bin((c / "tau-names.bin").read_bytes(), ix.build_id, (len(ix.tracks), len(ix.albums), len(ix.artists))) is not None)
print("PASSED" if not fails else f"FAILED ({fails})"); sys.exit(1 if fails else 0)
