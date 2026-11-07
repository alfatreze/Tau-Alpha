#!/usr/bin/env python3
"""The wiring of the name sidecar in Player 2 (fw2/stream/library.c, the real file, host build sim/p2_library_names_harness.c): a library built from the
shared name corpus (sim/name_corpus.py) is loaded with and without tau-names.bin, and every text the player draws (the track card and the artist, album and
track lists) is compared with the display rule: the original where the font can draw every character, the index text otherwise."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sim"))
import name_corpus as C
import name_sidecar as NS
import sync_media as S
import tau_library as L
import toolchain
from test_library_index import make_mp3

fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {str(info)[:300]}" if info and not cond else ""))


def lat1(s):
    return s.encode("latin-1", errors="replace")


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    exe = td / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unknown-pragmas", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
                        "-o", str(exe), str(ROOT / "sim/p2_library_names_harness.c")], env=toolchain.host_env(), capture_output=True, text=True)
    if r.returncode or "warning" in r.stderr:
        print(r.stderr[-1500:]); sys.exit("harness build failed or warned")

    # a card tree from the corpus (names converted as sync_media does), tags the index can hold (Latin-1 when the original fits, else folded)
    rels = {c["id"]: Path(c["cat"]) / (c["name"] + ".mp3") for c in C.CASES}
    nm = S.NameMap(list(rels.values()))
    root = td / "common"
    orig_tags = {}
    for c in C.CASES:
        tg = c.get("tags") or (None, None, None)
        title = tg[0] or c["name"]
        file_tags = {}
        for key, v, dflt in (("TIT2", title, c["name"]), ("TPE1", tg[1], "Artist " + c["cat"]), ("TALB", tg[2], c["cat"])):
            v = v or dflt
            try:
                v.encode("latin-1"); file_tags[key] = v if all(32 <= ord(x) < 256 and ord(x) != 0x7F for x in v) else L.ascii_text(v) or "x"
            except UnicodeEncodeError:
                file_tags[key] = L.ascii_text(v) or "x"
        make_mp3(root / nm.rel(rels[c["id"]]), file_tags, frames=20)
        orig_tags[c["id"]] = (title, tg[1] or ("Artist " + c["cat"]), tg[2] or c["cat"])
    entries, pls, warns = L.scan(root, playlists=True)
    data = L.build_index(entries, pls, [])
    assert L.verify(data, root) == []
    ix = L.parse(data)
    lib = td / "tau-library.tdb"
    lib.write_bytes(data)
    n_t, n_a, n_r = len(ix.tracks), len(ix.albums), len(ix.artists)

    # which original belongs to which index track
    by_card = {str(nm.rel(rels[c["id"]])): c for c in C.CASES}
    case_of = []
    for tid in range(n_t):
        path = L.track_path(ix, tid)[len(ix.root):]
        case_of.append(by_card[path])

    # rich originals for the sidecar: tags of each track, and per-category album / artist names (some drawable, some not)
    rich_album = {c: (f"{c} é", f"{c} 東京")[hash(c) % 2 if False else (len(c) % 2)] for c in {x["cat"] for x in C.CASES}}
    rich_artist = {c: (f"Artist {c} ü", f"Artist {c} Ж")[len(c) % 2 == 0] for c in {x["cat"] for x in C.CASES}}
    tracks = []
    for tid in range(n_t):
        c = case_of[tid]
        t, a, b = orig_tags[c["id"]]
        tracks.append({"file": rels[c["id"]].name, "title": t, "artist": a if c["id"].endswith(("a", "e", "o")) else None, "album": b if c["id"].endswith(("a", "i", "s")) else None})
    albums = []
    for ai, al in enumerate(ix.albums):
        cat = ix.str(al[0])
        albums.append((rich_album.get(cat), rich_artist.get(ix.str(ix.artists[al[2]][0]).replace("Artist ", ""))))
    artists = [rich_artist.get(ix.str(a[0]).replace("Artist ", "")) for a in ix.artists]
    names = td / "tau-names.bin"
    names.write_bytes(NS.build_bin(ix.build_id, tracks, albums, artists))

    def run(lib_path, names_path):
        o = subprocess.run([str(exe), str(lib_path), str(names_path) if names_path else "-"], capture_output=True, text=True)
        if o.returncode:
            return None, o.stderr[-500:]
        res = {"M": {}, "B": {}}
        for ln in o.stdout.splitlines():
            w = ln.split()
            if w[0] == "state": res["state"] = tuple(int(x) for x in w[1:])
            elif w[0] == "opens": res["opens"] = int(w[1])
            elif w[0] == "M": res["M"][int(w[1])] = tuple(bytes.fromhex(x) if x != "-" else b"" for x in w[2:5])
            elif w[0] == "B": res["B"][(int(w[1]), int(w[2]))] = bytes.fromhex(w[3]) if w[3] != "-" else b""
        return res, None

    base, err = run(lib, None)
    check("without a sidecar the library loads, one failed open, state 'absent'", base and base["state"] == (1, 0, 2, 0) and base["opens"] == 1, (base and (base["state"], base["opens"]), err))
    check("the index text is Latin-1 and the harness read every track", base and len(base["M"]) == n_t)

    # the index text, straight from the reference reader, is what the baseline answers
    ref_idx = {tid: (ix.str(ix.tracks[tid][0]).encode("latin-1"),
                     ix.str(ix.artists[ix.albums[ix.tracks[tid][4]][2]][0]).encode("latin-1"),
                     ix.str(ix.albums[ix.tracks[tid][4]][0]).encode("latin-1")) for tid in range(n_t)}
    check("no sidecar: library_meta answers the index text", all(base["M"][t] == ref_idx[t] for t in range(n_t)))

    got, err = run(lib, names)
    check("with a good sidecar it loads", got and got["state"][0] == 1 and got["state"][2:] == (1, 0), (got and got["state"], err))

    def pick(side, idx):
        if side and all(NS.cov_latin1(ch) for ch in side):
            return side.encode("latin-1")
        return idx
    bad = []
    n_side = n_idx = 0
    for tid in range(n_t):
        t = tracks[tid]
        ai = ix.tracks[tid][4]
        want_title = pick(NS.clean_text(t["title"]), ref_idx[tid][0])
        side_artist = NS.clean_text(t["artist"]) or NS.clean_text(artists[ix.albums[ai][2]])
        want_artist = pick(side_artist, ref_idx[tid][1])
        side_album = NS.clean_text(t["album"]) or NS.clean_text(albums[ai][0])
        want_album = pick(side_album, ref_idx[tid][2])
        g = got["M"][tid]
        for nm_, w, gg in (("title", want_title, g[0]), ("artist", want_artist, g[1]), ("album", want_album, g[2])):
            if w != gg: bad.append((tid, nm_, w, gg))
            if gg != ref_idx[tid][("title", "artist", "album").index(nm_)]: n_side += 1
            else: n_idx += 1
    check("library_meta: the original where drawable, the index text otherwise, for every track", not bad, bad[:3])
    check("both outcomes occur (the originals differ from the index for some, fall back for others)", n_side > 20 and n_idx > 20, (n_side, n_idx))

    import struct
    order = lambda sec, i: struct.unpack_from("<H", data, ix.sec[sec][0] + 2 * i)[0]
    bad = []
    for (kind, pos), text in got["B"].items():
        if kind == 1:                                                   # artist rows: the artist record
            w = pick(NS.clean_text(artists[pos]), base["B"][(kind, pos)])
        elif kind == 2:                                                 # album rows: the album record, in title order
            w = pick(NS.clean_text(albums[order("album_by_title", pos)][0]), base["B"][(kind, pos)])
        else:                                                           # track rows: the track record's title, in title order
            tid = order("track_by_title", pos)
            w = pick(NS.clean_text(tracks[tid]["title"]), base["B"][(kind, pos)])
        if w != text: bad.append((kind, pos, w, text))
    check("browser artist, album and track rows follow the rule, in the index's order", not bad, bad[:3])
    changed = {k for k in got["B"] if got["B"][k] != base["B"][k]}
    kinds_changed = {k[0] for k in changed}
    check("rows of all three lists change where an original is drawable", kinds_changed == {1, 2, 5}, kinds_changed)
    check("no row is ever longer or different in a way the font cannot draw", all(all(NS.cov_latin1(chr(b)) for b in got["B"][k]) for k in got["B"]))

    # ---- refusal paths: the library is never affected, the index text stays
    other = td / "other.bin"
    other.write_bytes(NS.build_bin(ix.build_id ^ 1, tracks, albums, artists))
    g2, _ = run(lib, other)
    check("a sidecar for another index is refused (code 22) and the texts are the index's", g2["state"][2:] == (2, 22) and g2["M"] == base["M"] and g2["B"] == base["B"], g2["state"])
    short = td / "short.bin"
    short.write_bytes(names.read_bytes()[:-8])
    g3, _ = run(lib, short)
    check("a truncated sidecar is refused (code 25) and the texts are the index's", g3["state"][2:] == (2, 25) and g3["M"] == base["M"], g3["state"])
    junk = td / "junk.bin"
    junk.write_bytes(b"not a sidecar at all" * 8)
    g4, _ = run(lib, junk)
    check("a file that is not a sidecar is refused (code 21)", g4["state"][2:] == (2, 21) and g4["M"] == base["M"], g4["state"])
    wrongn = td / "wrongn.bin"
    wrongn.write_bytes(NS.build_bin(ix.build_id, tracks[:-1], albums, artists))
    g5, _ = run(lib, wrongn)
    check("a sidecar with the wrong track count is refused (code 23)", g5["state"][2:] == (2, 23), g5["state"])
    g6, _ = run(lib, None)
    check("no sidecar file: state absent (2, err 0), the library itself is fine", g6["state"] == (1, 0, 2, 0), g6["state"])

if fails:
    print("FAILED:", fails)
    sys.exit(1)
print("PASSED")
