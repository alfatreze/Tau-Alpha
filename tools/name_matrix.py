#!/usr/bin/env python3
"""The card test matrix for BUG-001, step 2 (docs/issues/001-unicode-playlist-paths.md): files whose names are NOT plain ASCII, so a diagnostic build (step 3) can try to open each one by its exact path and say what the Pocket does.

  python3 tools/name_matrix.py plan                       the variants: id, what to expect, the path and its raw bytes (nothing is written)
  python3 tools/name_matrix.py write  --card /Volumes/Pock          dry run: what would be written
  python3 tools/name_matrix.py write  --card /Volumes/Pock --yes    write the folder, then read every name back
  python3 tools/name_matrix.py verify --card /Volumes/Pock          read the names back: which exist under the exact bytes, which the OS changed (a normalised or refused name is a finding)
  python3 tools/name_matrix.py remove --card /Volumes/Pock --yes    delete the folder

Everything goes under <media root>/name-test/ (the media root is the `root` of the card's tau-library.tdb, normally /Assets/tau/common/): per variant a tiny valid silent MP3 whose ID3 title is "NM<id>" (the diagnostic reads it back
to prove it opened THAT file), `matrix.tnm` (2,048 bytes, padded with newlines; what the firmware reads: first line "TNM1 <count>", then one line per variant "<id 2 digits>|<o or f>|<absolute path, raw bytes>"; o = expect the open to work, f = a negative control
that must fail cleanly) and MATRIX.txt (the paths and their bytes for the record). The variants: ASCII control; NFC and NFD (what macOS writes) accents; several accents; Cyrillic; CJK; an emoji (4-byte UTF-8); accented FOLDERS
(NFC, NFD, with an accented file inside); punctuation; paths of 220 bytes in ASCII and of about 210 bytes in 3-byte characters (near the 222-byte limit of fw2/stream/slot.c); and negative controls: a path over the limit, a
missing file, a path that is not valid UTF-8 (a raw Latin-1 byte). The library index builder (tools/tau_library.py) scans common/ and WOULD list these files as tracks (folded to ASCII): remove the folder before rebuilding the
index, or keep it to see them. The card is not ejected: eject it yourself. Nothing here runs on the Pocket."""
import argparse, hashlib, shutil, struct, sys, unicodedata
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_library as tl
DIR = "name-test"
MANIFEST = 2048                  # the length of matrix.tnm (padded with newlines)
LIMIT = 222                       # the longest path slot_open_path accepts (sim/test_p2_name_paths.py, ASCII template shape assumed)
N = lambda s: unicodedata.normalize("NFC", s)
D = lambda s: unicodedata.normalize("NFD", s)


def variants(root="/Assets/tau/common/"):
    """[(id, expect 'o'|'f', what, path bytes)] with the absolute path as the library would name it"""
    base = root + DIR + "/"
    out = []
    def add(what, rel, expect="o"):
        out.append((len(out), expect, what, (base + rel).encode("utf-8") if isinstance(rel, str) else base.encode("utf-8") + rel))
    add("ASCII control", "ctl_ascii.mp3")
    add("accent, NFC (precomposed)", N("nfc_Café.mp3"))
    add("accent, NFD (decomposed, what macOS writes)", D("nfd_Café.mp3"))
    add("several accents (NFC)", N("Björk Ünïcode ñ ß.mp3"))
    add("Cyrillic", "Привет.mp3")
    add("CJK (3-byte)", "日本語.mp3")
    add("emoji (4-byte UTF-8, a surrogate pair in FAT's UTF-16)", "note_\U0001F3B5.mp3")
    add("accented folder (NFC), ASCII file", N("Álbum/01 Intro.mp3"))
    add("accented folder and file (NFC)", N("Álbum Ñ/Café.mp3"))
    add("accented folder (NFD), ASCII file", D("Café Dir/a.mp3"))
    add("punctuation and spaces", "a b & c (1) [x] 'q' #5.mp3")
    n = LIMIT - 2 - len((base + ".mp3").encode())
    add("ASCII path of %d bytes (near the limit)" % (len(base) + n + 4 - 0), "L" * n + ".mp3")
    cjk = "日" * ((LIMIT - 12 - len(base)) // 3)
    add("CJK path of about %d bytes (near the limit, 3 bytes a character)" % (len(base) + len(cjk) * 3 + 4), cjk + ".mp3")
    add("negative control: an ASCII path over the %d-byte limit (SLOT_E_TOOLONG expected)" % LIMIT, "T" * (LIMIT + 10 - len(base) - 4) + ".mp3", "f")
    add("negative control: no such file", "does_not_exist.mp3", "f")
    add("negative control: not valid UTF-8 (a raw Latin-1 byte 0xE9)", b"Caf\xe9.mp3", "f")
    return out


def id3_title(text):
    """an ID3v2.3 tag with one TIT2 frame: the variant's id, which the diagnostic reads back"""
    body = b"\x00" + text.encode("latin-1")
    frame = b"TIT2" + struct.pack(">I", len(body)) + b"\x00\x00" + body
    n = len(frame); return b"ID3\x03\x00\x00" + bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127]) + frame


def mp3(vid):
    """a valid silent MPEG-1 layer III file: the tag, then 26 frames of 128 kbps / 44.1 kHz (417 bytes each, side info and data zero)"""
    return id3_title("NM%02d" % vid) + (b"\xff\xfb\x90\x00" + b"\x00" * 413) * 26


def files(root="/Assets/tau/common/"):
    """{relative path under name-test/ as bytes: content} for every variant that is a file to write, plus the manifest and the record"""
    out = {}; vs = variants(root); prefix = (root + DIR + "/").encode()
    for vid, expect, what, path in vs:
        if expect == "o": out[path[len(prefix):]] = mp3(vid)
    man = b"TNM1 %d\n" % len(vs) + b"".join(b"%02d|%s|%s\n" % (vid, expect.encode(), path) for vid, expect, what, path in vs)
    assert len(man) <= MANIFEST, "matrix.tnm outgrew its %d bytes" % MANIFEST
    out[b"matrix.tnm"] = man + b"\n" * (MANIFEST - len(man))                     # exactly MANIFEST bytes: the firmware (fw2/diagnostics/name_matrix.c) reads a fixed length
    rec = ["BUG-001 name matrix (tools/name_matrix.py): id | expect | what | path | raw UTF-8 bytes (hex)"]
    for vid, expect, what, path in vs: rec.append("%02d | %s | %s | %s | %s" % (vid, expect, what, path.decode("utf-8", "backslashreplace"), path.hex()))
    out[b"MATRIX.txt"] = ("\n".join(rec) + "\n").encode("utf-8")
    return out


def media_root(card):
    for plat in ("tau", "tau_dev"):
        f = Path(card, "Assets", plat, "common", "tau-library.tdb")
        if not f.is_file(): continue
        root = tl.parse(f.read_bytes()).root
        d = Path(card) / root.strip("/")
        if d.is_dir(): return root, d
    return None, None


def osname(b): return b.decode("utf-8", "surrogateescape") if isinstance(b, bytes) else b


def read_back(dest, fs):
    """per file: (exists under the exact bytes, content equal); and the names the OS actually lists, as raw bytes, for the record"""
    import os
    res = {}
    for rel, data in fs.items():
        p = os.path.join(os.fsencode(str(dest)), rel)
        try:
            with open(p, "rb") as f: res[rel] = (True, f.read() == data)
        except OSError: res[rel] = (False, False)
    listed = []
    for dp, dn, fn in os.walk(os.fsencode(str(dest))):
        for n in dn + fn: listed.append(os.path.relpath(os.path.join(dp, n), os.fsencode(str(dest))))
    return res, sorted(listed)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0]); ap.add_argument("cmd", choices=("plan", "write", "verify", "remove")); ap.add_argument("--card", type=Path); ap.add_argument("--yes", action="store_true")
    a = ap.parse_args(argv)
    if a.cmd == "plan":
        for vid, expect, what, path in variants():
            print("%02d  %s  %-62s %3d bytes  %s" % (vid, expect, what, len(path), path.decode("utf-8", "backslashreplace")[:90]))
        print("\n%d variants; o = the open must work, f = a negative control that must fail cleanly; nothing is written (use write)" % len(variants())); return 0
    if not a.card: print("--card is required"); return 2
    root, common = media_root(a.card)
    if not common: print("no library (Assets/*/common/tau-library.tdb) on the card: the firmware needs it to know the media root"); return 1
    dest = common / DIR; fs = files(root)
    print("media root %s -> %s" % (root, dest) + ("" if a.yes or a.cmd == "verify" else "   (dry run: nothing will be written; --yes writes)"))
    if a.cmd == "remove":
        print("remove %s" % dest)
        if a.yes and dest.exists(): shutil.rmtree(dest)
        return 0
    if a.cmd == "write":
        print("%d files (%d bytes) in %d variants" % (len(fs), sum(len(v) for v in fs.values()), len(variants())))
        if not a.yes: return 0
        import os
        for rel, data in fs.items():
            p = os.path.join(os.fsencode(str(dest)), rel); os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f: f.write(data)
    res, listed = read_back(dest, fs)
    miss = [r for r, (ex, same) in res.items() if not ex]; diff = [r for r, (ex, same) in res.items() if ex and not same]
    print("read back: %d of %d files exist under the exact bytes written, %d differ in content" % (len(res) - len(miss), len(res), len(diff)))
    for r in miss: print("  NOT FOUND under the exact bytes (the OS refused or changed the name): %s  %s" % (osname(r), r.hex()))
    print("the OS lists these entries (raw bytes, hex):")
    for n in listed: print("  %s  %s" % (osname(n), n.hex()))
    return 1 if (miss or diff) else 0


if __name__ == "__main__":
    sys.exit(main())
