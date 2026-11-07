#!/usr/bin/env python3
"""BUG-001 contract (docs/issues/001, B-1000): the Pocket decodes a path as code page 437. The index carries path pieces as
cp437 bytes (NFC), sync_media keeps every cp437 character and folds only the rest, names outside cp437 are never indexed."""
import sys
import tempfile
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
sys.path.insert(0, str(HERE))
import tau_library as L
import sync_media as S
from test_library_index import make_mp3

fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {info}" if info and not cond else ""))


# --- sync_media: keep what the Pocket opens, fold the rest per character
cp = S.card_part
check("keeps e-acute, a-umlaut, n-tilde, ss, ae, pi",
      cp("Café Nausicaä Año Straße Sæm Pi π.mp3") == "Café Nausicaä Año Straße Sæm Pi π.mp3")
check("decomposed input is written precomposed", cp("Café.mp3") == "Café.mp3")
HAVE_ANY, HAVE_KAKASI = S._anyascii() is not None, S._kakasi() is not None
check("A-acute / C-caron fold to the plain letter", cp("Álvaro Čapek.mp3") == "Alvaro Capek.mp3")
check("O-slash folds to O (not removed)", cp("Søren Ørsted.mp3") == "Soren Orsted.mp3")
check("without the libraries only the unreachable characters are dropped", HAVE_ANY or cp("Café Привет.mp3") == "Café .mp3")
check("an unreachable-only name becomes track.ext without the libraries", HAVE_ANY or cp("Привет.mp3") == "track.mp3")
if HAVE_ANY:
    check("Cyrillic is transliterated (anyascii)", cp("Привет.mp3") == "Privet.mp3", cp("Привет.mp3"))
    check("Chinese becomes pinyin (anyascii)", cp("东京.flac") == "DongJing.flac", cp("东京.flac"))
    check("emoji are dropped, the rest of the name stays", cp("Hi 😀 there.mp3") == "Hi there.mp3", cp("Hi 😀 there.mp3"))
    check("an emoji-only name becomes track.ext", cp("😀.mp3") == "track.mp3")
    check("only the unreachable run is transliterated", cp("Café Привет.mp3") == "Café Privet.mp3", cp("Café Привет.mp3"))
else:
    print("skip anyascii checks (not installed)")
if HAVE_KAKASI:
    check("Japanese gets its Japanese reading (pykakasi)", cp("東京タワー.mp3") == "toukyou tawaa.mp3", cp("東京タワー.mp3"))
else:
    print("skip pykakasi checks (not installed)")
check("forbidden characters become _", cp('a<b>c:d.mp3') == "a_b_c_d.mp3")
check("--ascii-only keeps the old behaviour", cp("Café Straße.mp3", True) == "Cafe Strae.mp3")
check("every kept name is reachable", all(L.reachable(cp(n)) for n in ["Café", "Привет", "Søren", "日本語.flac", "😀.mp3", "Ünïcödé"]))

# --- name map: collisions are numbered, never fatal, same every run, case-insensitive per folder
rels = [Path("A/Café.mp3"), Path("A/Cafe\u0301.mp3"), Path("A/CAFÉ.mp3"), Path("A/x.mp3"), Path("B/x.mp3"), Path("A/track.mp3"), Path("A/😀.mp3"), Path("A/😁.mp3")]
m1 = S.NameMap(rels)
m2 = S.NameMap(list(reversed(rels)))
check("a collision is numbered", str(m1.rel(Path("A/CAFÉ.mp3"))) != str(m1.rel(Path("A/Café.mp3"))) and "(2)" in "".join(str(m1.rel(r)) for r in rels))
outs = [str(m1.rel(r)).lower() for r in rels]
check("no two different originals share a converted name", len(set(outs)) == len({tuple(__import__("unicodedata").normalize("NFC", x) for x in r.parts) for r in rels}) and len(set(outs)) >= 7, outs)
check("the numbering does not depend on input order", all(m1.rel(r) == m2.rel(r) for r in rels))
check("the same name in another folder is not numbered", str(m1.rel(Path("B/x.mp3"))) == "B/x.mp3")
check("playlist lines follow the numbered names", m1.line(("A",), ["x.mp3"]) == "x.mp3" and m1.line(("A",), ["track.mp3"]) == str(m1.rel(Path("A/track.mp3")).name))

# --- index: path pieces are cp437 bytes, track_path round-trips, unreachable names are left out
with tempfile.TemporaryDirectory() as td:
    root = Path(td)
    names = ["Café/01 Café.mp3", "Straße/02 Äpfel.mp3", "plain/03 plain.mp3", "Caféb/04 x.mp3"]
    for n in names:
        make_mp3(root / n, {"TIT2": "t", "TPE1": "a", "TALB": "b"})
    make_mp3(root / "Привет/05 x.mp3", {"TIT2": "t", "TPE1": "a", "TALB": "b"})
    entries, pls, warns = L.scan(root, playlists=True)
    check("a Cyrillic path is left out with a warning", len(entries) == 4 and any("code page 437" in w for w in warns), warns)
    data = L.build_index(entries, pls, [])
    check("verify passes", L.verify(data, root) == [], L.verify(data, root))
    ix = L.parse(data)
    paths = {L.track_path(ix, i) for i in range(len(ix.tracks))}
    check("track_path round-trips the precomposed names", "/Assets/tau/common/Café/01 Café.mp3" in paths, paths)
    pool = ix.strings
    check("e-acute is the single byte 0x82 in the pool", b"Caf\x82/" not in pool and b"Caf\x82\x00" in pool)
    check("ss is 0xE1 and A-umlaut 0x8E", b"Stra\xe1e\x00" in pool and b"02 \x8epfel.mp3\x00" in pool)
    check("no UTF-8 or Latin-1 form of a path piece is in the pool", b"\xc3\xa9" not in pool and b"Caf\xe9" not in pool)
    check("a decomposed folder name is stored as the same single byte", b"Caf\x82b\x00" in pool)
    check("build is deterministic", data == L.build_index(*L.scan(root, playlists=True)[:2], []))

if fails:
    print("FAILED:", fails)
    sys.exit(1)
print("PASSED")
