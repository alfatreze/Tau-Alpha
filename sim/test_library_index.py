#!/usr/bin/env python3
"""Host tests for tools/tau_library.py (spec docs/MEDIA_LIBRARY_0.4_SPEC.md): tag readers on fabricated files,
build determinism, every verify invariant, the corruption matrix (each fault must give its spec E-code),
playlist import, the caps and the 7,180-track size budget."""
import struct
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import tau_library as L

fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {info}" if info and not cond else ""))


def ss(n):
    return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])


def frame(fid, text):
    b = b"\x00" + text.encode("latin-1")
    return fid.encode() + struct.pack(">I", len(b)) + b"\x00\x00" + b


def make_mp3(path, tags, frames=100, v24=False):
    body = b"".join(frame(k, v) for k, v in tags.items())
    if v24:
        body = b"".join(k.encode() + ss(len(b"\x00" + v.encode())) + b"\x00\x00" + b"\x00" + v.encode()
                        for k, v in tags.items())
    hdr = b"ID3" + bytes([4 if v24 else 3, 0, 0]) + ss(len(body)) + body
    audio = (b"\xff\xfb\x90\x64" + b"\x00" * 413) * frames          # MPEG1 L3 128 kbps 44.1 kHz, 417 B frames
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(hdr + audio)


def make_flac(path, tags, seconds=60):
    sr = 44100
    total = sr * seconds
    si = struct.pack(">HH", 4096, 4096) + b"\x00" * 6 + bytes([(sr >> 12) & 255, (sr >> 4) & 255,
                     ((sr & 15) << 4) | (1 << 1) | ((15 >> 4) & 1), ((15 & 15) << 4) | ((total >> 32) & 15)]) + \
        struct.pack(">I", total & 0xFFFFFFFF) + b"\x00" * 16
    vc = struct.pack("<I", 3) + b"tau" + struct.pack("<I", len(tags))
    for k, v in tags.items():
        kv = f"{k}={v}".encode()
        vc += struct.pack("<I", len(kv)) + kv
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"fLaC" + bytes([0, 0, 0, 34]) + si + bytes([0x84]) + len(vc).to_bytes(3, "big") + vc + b"\x00" * 64)


def tree(root):
    make_mp3(root / "Aurora/Skies/02 Second.mp3", dict(TIT2="Second", TPE1="Aurora", TALB="Skies", TRCK="2/3", TYER="2015"))
    make_mp3(root / "Aurora/Skies/01 First.mp3", dict(TIT2="First", TPE1="Aurora", TALB="Skies", TRCK="1/3", TYER="2015"), v24=True)
    make_mp3(root / "Aurora/Skies/03 Third.mp3", dict(TIT2="Third", TPE1="Aurora", TALB="Skies", TRCK="3/3", TYER="2015"))
    make_flac(root / "The Beta/Live/track a.flac", dict(TITLE="Alpha", ARTIST="The Beta", ALBUM="Live", TRACKNUMBER="1",
                                                        DATE="2001-05-01"))
    make_mp3(root / "loose.mp3", dict())                               # no tags at all
    make_mp3(root / "Zed/Z/01 x.mp3", dict(TIT2="9 Lives", TPE1="Zed", TALB="Z", TRCK="1"))
    (root / "Aurora/Skies/._01 First.mp3").write_bytes(b"junk")        # macOS junk must be ignored
    (root / "Aurora/Skies/playlist.m3u").write_text("#EXTM3U\n03 Third.mp3\n01 First.mp3\nmissing.mp3\n")
    (root / "Zed/Z/playlist.m3u").write_text("01 x.mp3\n")               # a generated album list: not imported
    (root / "Favs.m3u").write_text("Aurora/Skies/03 Third.mp3\nZed/Z/01 x.mp3\nAurora/Skies/03 Third.mp3\n")   # a real list, with a repeat


def main():
    with tempfile.TemporaryDirectory() as td:
        root = Path(td) / "common"
        tree(root)
        entries, pls, warns = L.scan(root, playlists=True)
        check("scan finds 6 audio files, ignores ._ junk", len(entries) == 6, len(entries))
        d1 = L.build_index(entries, pls, [])
        e2, p2, _ = L.scan(root, playlists=True)
        check("build is deterministic", d1 == L.build_index(e2, p2, []))
        check("verify passes (incl. every path exists)", L.verify(d1, root) == [], L.verify(d1, root))
        ix = L.parse(d1)
        check("counts 6 tracks / 4 albums / 4 artists", ix.n == dict(artists=4, albums=4, tracks=6, playlists=2), ix.n)
        names = [ix.str(a[0]) for a in ix.artists]
        check("artists sorted, 'The ' ignored ('Beta' sorts as B)", names == ["Aurora", "The Beta", "Unknown Artist", "Zed"], names)
        a0 = ix.albums[0]
        order = [ix.str(ix.tracks[i][0]) for i in range(a0[4], a0[4] + a0[5])]
        check("album tracks in track-number order (v2.3 and v2.4 tags)", order == ["First", "Second", "Third"], order)
        d2 = L.build_index(entries, pls, [], "/Assets/tau_psram_07/common/")
        check("root prefix follows the platform folder", L.track_path(L.parse(d2), 0) == "/Assets/tau_psram_07/common/Aurora/Skies/01 First.mp3")
        check("track path is root + dir + file", L.track_path(ix, 0) == "/Assets/tau/common/Aurora/Skies/01 First.mp3")
        check("root-level file has no double slash", "/Assets/tau/common/loose.mp3" in
              [L.track_path(ix, i) for i in range(6)])
        check("MP3 CBR duration ~ 100 frames of 26 ms = 2 s", ix.tracks[0][2] in (3, 2), ix.tracks[0][2])
        fl = [t for t in ix.tracks if t[5] == 2][0]
        check("FLAC duration from STREAMINFO = 60 s, format 2", fl[2] == 60, fl[2])
        check("year read (2015) and FLAC DATE year (2001)", ix.albums[0][3] == 2015 and 2001 in [a[3] for a in ix.albums])
        check("track number in tno, disc 0", ix.tracks[0][3] == 1 and ix.tracks[2][3] == 3)
        check("untagged file: title from name, tile number 0", any(ix.str(t[0]) == "loose" and t[3] == 0 for t in ix.tracks))
        names = [ix.str(l[0]) for l in ix.lists]
        check("user playlists imported (Favs, Skies), generated album list skipped", names == ["Favs", "Skies"] or sorted(names) == ["Favs", "Skies"], names)
        check("playlist resolved to ids, missing line dropped", ix.n["playlists"] == 2 and any("dropped" in w for w in warns))
        fav = next(l for l in ix.lists if ix.str(l[0]) == "Favs")
        check("repeats kept in a list (3 entries, first and last equal)", fav[2] == 3 and ix.items[fav[1]] == ix.items[fav[1] + 2])
        lo, _ = ix.sec["letters"]
        lt = struct.unpack_from("<81H", d1, lo)
        check("letter table: '#' first, 'Z' points at Zed", lt[0] == 0 and lt[26] == 3, lt[:27])
        check("digits sort under '#': '9 Lives' first among track titles",
              ix.str(ix.tracks[struct.unpack_from("<H", d1, ix.sec["track_by_title"][0])[0]][0]) == "9 Lives")

        # ---- corruption matrix: each fault -> expected E-code
        def flip(off, mask=0xFF, data=d1):
            b = bytearray(data)
            b[off] ^= mask
            return bytes(b)

        def code(data):
            try:
                L.parse(data)
                return 0
            except L.LibError as e:
                return e.code

        def fix_hdr(b):                                  # re-seal the header CRC after editing a header field
            b = bytearray(b)
            struct.pack_into("<I", b, 124, zlib.crc32(bytes(b[:124])) & 0xFFFFFFFF)
            return bytes(b)
        tr_off = ix.sec["tracks"][0]
        cases = [
            ("bad magic -> E11", flip(0), 11),
            ("header field changed (CRC) -> E11", flip(33), 11),
            ("version above reader -> E11", fix_hdr(flip(6, 0x03)), 11),
            ("truncated file -> E12", d1[:-16], 12),
            ("appended bytes -> E12", d1 + b"\x00" * 16, 12),
            ("flipped body byte -> E13", flip(tr_off + 3), 13),
            ("flipped string byte -> E13", flip(ix.sec["strings"][0] + 5), 13),
            ("count above the cap -> E14", fix_hdr(_set16(d1, 34, 2049)), 14),
            ("section out of range -> E15", fix_hdr(_set32(d1, 48 + 8 * 2, len(d1) + 16)), 15),
            ("section misaligned -> E15", fix_hdr(_set32(d1, 48 + 8 * 2, tr_off + 4)), 15),
            ("short file -> E11", d1[:64], 11),
        ]
        for name, blob, want in cases:
            check(name, code(blob) == want, f"got E{code(blob)}")
        # body-CRC-valid but structurally bad records must reach E17 / verify
        def reseal(b):
            b = bytearray(b)
            struct.pack_into("<I", b, 24, zlib.crc32(bytes(b[128:])) & 0xFFFFFFFF)
            struct.pack_into("<I", b, 16, zlib.crc32(bytes(b[128:])) & 0xFFFFFFFF)
            return fix_hdr(bytes(b))
        bad_str = reseal(_set32(d1, tr_off, 0x7FFFFF))
        check("track string offset out of range -> E17", code(bad_str) == 17, code(bad_str))
        bad_alb = reseal(_set16(d1, tr_off + 12, 999))
        check("track album id out of range -> E17", code(bad_alb) == 17, code(bad_alb))
        swapped = bytearray(d1)
        ao = ix.sec["album_by_title"][0]
        swapped[ao:ao + 2], swapped[ao + 2:ao + 4] = swapped[ao + 2:ao + 4], swapped[ao:ao + 2]
        check("verify catches an unsorted order table", any("not sorted" in p for p in L.verify(reseal(bytes(swapped)))))
        check("verify catches a missing file", any("file missing" in p for p in
              (lambda: (Path(root / "Zed/Z/01 x.mp3").unlink(), L.verify(d1, root))[1])()))

        # ---- limits
        try:
            L.build_index([dict(rel="a/" + "x" * 190 + ".mp3", dir="a", file="x" * 190 + ".mp3", tags={}, secs=0, fmt=1)])
            check("path over 200 bytes is refused", False)
        except L.LibError as e:
            check("path over 200 bytes is refused (E14)", e.code == 14)
        big = L.synth(L.CAPS["tracks"] + 1, 800, 300)
        try:
            L.build_index(big)
            check("track cap enforced", False)
        except L.LibError as e:
            check("track cap enforced (E14)", e.code == 14)
        check("ASCII transliteration", L.ascii_text("Nausicaä – Mönch") == "Nausicaa Monch")

    # ---- size budget at the owner's library scale
    syn = L.build_index(L.synth(7180, 800, 300))
    check("synthetic 7,180 tracks verifies", L.verify(syn) == [])
    check("7,180-track index within 0.4-0.9 MiB", 400 * 1024 < len(syn) < 900 * 1024, len(syn))
    check("synthetic build is deterministic", syn == L.build_index(L.synth(7180, 800, 300)))
    big = L.build_index(L.synth(16384, 2048, 1024))
    check("cap-size library (16,384 tracks) verifies and is under 4 MiB", L.verify(big) == [] and len(big) < 4 << 20, len(big))
    print(f"size: 7,180 tracks = {len(syn):,} B; 16,384 tracks = {len(big):,} B")
    print("FAILED: " + ", ".join(fails) if fails else "all library index tests passed")
    return 1 if fails else 0


def _set16(d, off, v):
    b = bytearray(d)
    struct.pack_into("<H", b, off, v)
    return bytes(b)


def _set32(d, off, v):
    b = bytearray(d)
    struct.pack_into("<I", b, off, v)
    return bytes(b)


if __name__ == "__main__":
    sys.exit(main())
