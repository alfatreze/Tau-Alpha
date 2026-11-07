#!/usr/bin/env python3
"""The sidecar WRITERS, end to end (docs/features/NAME_SIDECAR_SPEC.md): tools/sync_media.py --library on a source tree built from the shared name corpus (UTF-8 tags,
the original names on disk) writes the card-safe tree, tau-library.tdb, tau-names.json and tau-names.bin; the files bind to that index, carry the originals only where they
differ from what the index shows, survive a rebuild and a removed file, and the Player 2 firmware core (host build of fw2/stream/library.c) draws what the rule says."""
import json
import struct
import subprocess
import sys
import tempfile
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sim"))
import name_corpus as C
import name_sidecar as NS
import tau_library as L
import toolchain

fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {str(info)[:300]}" if info and not cond else ""))


def ss(n):
    return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])


def utf8_mp3(path, title, artist, album, frames=30):
    def fr(fid, text):
        b = b"\x03" + text.encode("utf-8")
        return fid.encode() + ss(len(b)) + b"\x00\x00" + b
    body = fr("TIT2", title) + fr("TPE1", artist) + fr("TALB", album)
    hdr = b"ID3" + bytes([4, 0, 0]) + ss(len(body)) + body
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(hdr + (b"\xff\xfb\x90\x64" + b"\x00" * 413) * frames)


def sync(src, card, *extra):
    r = subprocess.run([sys.executable, str(ROOT / "tools/sync_media.py"), str(src), "--card", str(card), "--core", "alfatreze.TAU X", "--library", "--no-art-variants", *extra],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def make_card(td, name="card"):
    card = td / name
    (card / "Cores" / "alfatreze.TAU X").mkdir(parents=True)
    (card / "Cores" / "alfatreze.TAU X" / "core.json").write_text(json.dumps({"core": {"metadata": {"platform_ids": ["tau"]}}}))
    (card / "Assets" / "tau" / "common").mkdir(parents=True)
    return card, card / "Assets" / "tau" / "common"


# cases a normal file system keeps apart (not NFC/NFD twins, not case twins) and can create (name length)
SKIP = {"nfd_cafe", "norm_b", "case_b", "long_ascii", "long_cjk", "long_cp437", "dotfile"}
cases = [c for c in C.CASES if c["id"] not in SKIP]

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    src = td / "src"
    orig_rel, tags = {}, {}
    for c in cases:
        tg = c.get("tags") or (None, None, None)
        title = NS.clean_text(tg[0]) or NS.clean_text(c["name"]) or "x"
        artist = NS.clean_text(tg[1]) or "Artist " + c["cat"]
        album = NS.clean_text(tg[2]) or c["cat"]
        rel = Path(c["cat"]) / (c["name"] + ".mp3")
        try:
            utf8_mp3(src / rel, title, artist, album)
        except OSError:
            continue
        orig_rel[c["id"]] = unicodedata.normalize("NFC", "src/" + rel.as_posix())           # sync_media keeps the source folder's name as the top folder
        tags[c["id"]] = (title, artist, album)
    LONG = "\u00c7a va tr\u00e8s bien \u00e9t\u00e9 " * 5                                                  # 100 characters, all Latin-1: the index keeps 63, the sidecar all
    utf8_mp3(src / "long" / "long title.mp3", LONG.strip(), "Artist long", "long")
    orig_rel["longtitle"] = "src/long/long title.mp3"
    check("the source tree was built from the corpus", len(orig_rel) >= 50, len(orig_rel))
    card, common = make_card(td)
    rc, out = sync(src, card)
    check("sync_media --library succeeds on the corpus", rc == 0, out[-600:])
    files = {p.name for p in common.iterdir() if p.is_file()}
    check("it wrote the index and both sidecar files", {"tau-library.tdb", "tau-names.json", "tau-names.bin"} <= files, files)
    data = (common / "tau-library.tdb").read_bytes()
    ix = L.parse(data)
    check("the index verifies against the card tree", L.verify(data, common) == [], L.verify(data, common)[:2])
    man = NS.load_manifest((common / "tau-names.json").read_bytes())
    counts = (len(ix.tracks), len(ix.albums), len(ix.artists))
    side = NS.parse_bin((common / "tau-names.bin").read_bytes(), ix.build_id, counts)
    check("the manifest and the binary bind to this index (build_id)", man["index_build_id"] == ix.build_id)
    paths = {L.track_path(ix, i): i for i in range(len(ix.tracks))}
    check("every record points at its track id", all(paths.get(ix.root + r["card"]) == r["id"] for r in man["tracks"]), man["tracks"][:1])
    rec = {r["card"]: r for r in man["tracks"]}
    by_orig = {v: k for k, v in orig_rel.items()}

    def card_of(cid):
        return next(r for r in man["tracks"] if r["orig"] == orig_rel[cid])

    cyr = card_of("cyrillic")
    check("a Cyrillic file keeps its original path and tags in the manifest", cyr["orig"].endswith("Привет мир.mp3") and cyr["title"] == "Привет" and cyr["card"] != cyr["orig"], cyr)
    tid = cyr["id"]
    check("the binary carries the original title and file name of that track", side["tracks"][tid]["title"] == "Привет" and side["tracks"][tid]["file"] == "Привет мир.mp3", side["tracks"][tid])
    plain_cards = [L.track_path(ix, i)[len(ix.root):] for i in range(len(ix.tracks))]
    ascii_ids = [paths[ix.root + c] for c in ("src/plain/01 Plain Song.mp3",) if (ix.root + c) in paths]
    check("a plain ASCII track has no record and no field in the binary", ascii_ids and "src/plain/01 Plain Song.mp3" not in rec and all(v is None for v in side["tracks"][ascii_ids[0]].values()), side["tracks"][ascii_ids[0]] if ascii_ids else None)
    cafe = next(i for i in range(len(ix.tracks)) if L.track_path(ix, i).endswith("/Café.mp3"))
    check("Latin-1 text the index shows as is is not repeated in the binary", side["tracks"][cafe]["title"] is None and side["tracks"][cafe]["file"] is None, side["tracks"][cafe])
    check("manifest originals are the source paths (NFC)", all(r["orig"] in by_orig for r in man["tracks"]))
    origs = set(orig_rel.values())
    renamed_cards = [p for p in plain_cards if p not in origs]
    check("every file whose card name differs from its original has a record", renamed_cards and all(p in rec for p in renamed_cards), [p for p in renamed_cards if p not in rec][:3])
    n_fields = sum(1 for t in side["tracks"] for v in t.values() if v)
    check("the binary is small and not a copy of the library", 0 < n_fields < 4 * len(ix.tracks) and len((common / "tau-names.bin").read_bytes()) < 64 * 1024, (n_fields, len((common / "tau-names.bin").read_bytes())))
    again = sync(src, card)
    check("a second sync gives byte-identical sidecar files (idempotent)", again[0] == 0 and (common / "tau-names.json").read_bytes() == NS.manifest_bytes(man), again[1][-300:])

    # ---- the firmware core reads what the writers wrote
    exe = td / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unknown-pragmas", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
                        "-o", str(exe), str(ROOT / "sim/p2_library_names_harness.c")], env=toolchain.host_env(), capture_output=True, text=True)
    check("the firmware harness builds", r.returncode == 0, r.stderr[-400:])
    o = subprocess.run([str(exe), str(common / "tau-library.tdb"), str(common / "tau-names.bin")], capture_output=True, text=True)
    lines = o.stdout.splitlines()
    check("the firmware loads the library and accepts the sidecar the tools wrote", lines and lines[0] == "state 1 0 1 0", lines[:1] or o.stderr[-300:])
    M = {int(w[1]): w[2:5] for w in (ln.split() for ln in lines if ln.startswith("M "))}
    dec = lambda h: bytes.fromhex(h) if h != "-" else b""
    lt = next(i for i in range(len(ix.tracks)) if L.track_path(ix, i).endswith("/long title.mp3"))
    check("a title longer than the index keeps is in the binary in full", side["tracks"][lt]["title"] == LONG.strip(), side["tracks"][lt])
    check("the firmware draws that full original (Latin-1), not the index's cut text", dec(M[lt][0]) == LONG.strip().encode("latin-1") and len(dec(M[lt][0])) > len(ix.str(ix.tracks[lt][0])), (len(dec(M[lt][0])), len(ix.str(ix.tracks[lt][0]))))
    check("a Cyrillic title shows the index text (the font cannot draw the original)", dec(M[tid][0]) == ix.str(ix.tracks[tid][0]).encode("latin-1"))

    # ---- rebuilding the index alone keeps the originals; a removed file drops its record
    r2 = subprocess.run([sys.executable, str(ROOT / "tools/tau_library.py"), "build", str(common), "--playlists"], capture_output=True, text=True)
    man2 = NS.load_manifest((common / "tau-names.json").read_bytes())
    check("rebuilding the index alone keeps every original path (merged from the old manifest)", r2.returncode == 0 and {r["card"]: r["orig"] for r in man2["tracks"]} == {r["card"]: r["orig"] for r in man["tracks"]}, r2.stderr[-300:])
    victim = next(r for r in man2["tracks"] if r["orig"] == orig_rel["cyrillic"])
    (common / victim["card"]).unlink()
    r3 = subprocess.run([sys.executable, str(ROOT / "tools/tau_library.py"), "build", str(common), "--playlists"], capture_output=True, text=True)
    man3 = NS.load_manifest((common / "tau-names.json").read_bytes())
    check("a file removed from the card loses its record at the next build", r3.returncode == 0 and not any(r["card"] == victim["card"] for r in man3["tracks"]), r3.stderr[-300:])
    ix3 = L.parse((common / "tau-library.tdb").read_bytes())
    NS.parse_bin((common / "tau-names.bin").read_bytes(), ix3.build_id, (len(ix3.tracks), len(ix3.albums), len(ix3.artists)))
    check("the binary is bound to the rebuilt index", True)

    # ---- --no-names writes nothing; a plain library removes stale files and writes none
    card2, common2 = make_card(td, "card2")
    rc, out = sync(src, card2, "--no-names")
    check("--no-names: index written, no sidecar files", rc == 0 and (common2 / "tau-library.tdb").exists() and not (common2 / "tau-names.json").exists() and not (common2 / "tau-names.bin").exists(), out[-300:])
    plain = td / "plain"
    for i in range(3):
        utf8_mp3(plain / "Album" / f"0{i + 1} Song {i}.mp3", f"Song {i}", "An Artist", "An Album")
    card3, common3 = make_card(td, "card3")
    (common3 / "tau-names.json").write_text("{}"); (common3 / "tau-names.bin").write_bytes(b"stale")
    rc, out = sync(plain, card3)
    check("a plain ASCII library writes no sidecar and removes stale ones", rc == 0 and not (common3 / "tau-names.json").exists() and not (common3 / "tau-names.bin").exists(), out[-300:])

    # ---- --ascii-only still records the originals
    card4, common4 = make_card(td, "card4")
    rc, out = sync(src, card4, "--ascii-only")
    m4 = NS.load_manifest((common4 / "tau-names.json").read_bytes()) if (common4 / "tau-names.json").exists() else {"tracks": []}
    check("--ascii-only: the manifest still holds the original names (accented ones now differ from the card name)", rc == 0 and any(r["orig"].endswith("Café/") or "ü" in r["orig"] for r in m4["tracks"]), out[-200:])

if fails:
    print("FAILED:", fails)
    sys.exit(1)
print("PASSED")
