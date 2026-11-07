#!/usr/bin/env python3
"""The card test matrix for BUG-001, step 2 (docs/issues/001-unicode-playlist-paths.md): files whose names are NOT plain ASCII, so a diagnostic build (step 3) can try to open each one by its exact path and say what the Pocket does.

  python3 tools/name_matrix.py plan                       the variants: id, what to expect, the path and its raw bytes (nothing is written)
  python3 tools/name_matrix.py write  --card /Volumes/Pock          dry run: what would be written
  python3 tools/name_matrix.py write  --card /Volumes/Pock --yes    write the folder, then read every name back
  python3 tools/name_matrix.py verify --card /Volumes/Pock          read the names back: which exist under the exact bytes, which the OS changed (a normalised or refused name is a finding)
  python3 tools/name_matrix.py remove --card /Volumes/Pock --yes    delete the folder

Everything goes under <media root>/name-test/ (the media root is the `root` of the card's tau-library.tdb, normally /Assets/tau/common/): per variant a tiny valid silent MP3 whose ID3 title is "NM<id>" (the diagnostic reads it back
to prove it opened THAT file), `matrix.tnm` (3,072 bytes, padded with newlines; what the firmware reads: first line "TNM2 <count>", then one line per variant "<id 2 digits>|<o, f or p>|<file id 2 digits>|<absolute path, raw bytes>"; o = expect the open to work, p = a probe (the result is the information), f = a negative control
that must fail cleanly) and MATRIX.txt (the paths and their bytes for the record). The variants: ASCII control; NFC and NFD (what macOS writes) accents; several accents; Cyrillic; CJK; an emoji (4-byte UTF-8); accented FOLDERS
(NFC, NFD, with an accented file inside); punctuation; paths of 250 bytes in ASCII and of about 250 bytes in 3-byte characters (the 0192 path field is 256 bytes); and negative controls: a path over the limit, a
missing file, a path that is not valid UTF-8 (a raw Latin-1 byte). The library index builder (tools/tau_library.py) scans common/ and WOULD list these files as tracks (folded to ASCII): remove the folder before rebuilding the
index, or keep it to see them. The card is not ejected: eject it yourself. Nothing here runs on the Pocket."""
import argparse, hashlib, shutil, struct, sys, unicodedata
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_library as tl
DIR = "name-test"
MANIFEST = 3072                  # the length of matrix.tnm (padded with newlines)
FID = {17: 16, 18: 16, 20: 19, 21: 19, 22: 1, 23: 2, 24: 3, 25: 9, 27: 26, 29: 28, 31: 30}      # a probe that names the same file as another variant (another encoding of its path): variant -> the variant whose file it is
LIMIT = 255                       # the path field of the 0192 open struct is 256 bytes, NUL-terminated (docs: open_dataslot_file_t); fw2/stream/slot.c refuses a longer one (SLOT_E_TOOLONG). The Pocket opened a 220-byte ASCII path (B-991)
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
    n = 250 - len((base + ".mp3").encode())
    add("ASCII path of %d bytes (near the limit)" % (len(base) + n + 4), "L" * n + ".mp3")
    cjk = "日" * ((250 - 4 - len(base)) // 3)
    add("CJK path of about %d bytes (near the limit, 3 bytes a character)" % (len(base) + len(cjk) * 3 + 4), cjk + ".mp3")
    add("negative control: an ASCII path over the 255-byte limit (262 bytes; SLOT_E_TOOLONG expected)", "T" * (262 - len(base) - 4) + ".mp3", "f")
    add("negative control: no such file", "does_not_exist.mp3", "f")
    add("negative control: not valid UTF-8 (a raw Latin-1 byte 0xE9)", b"Caf\xe9.mp3", "f")
    # round 2 (B-992): letters with no decomposition (NFC = NFD), so the bytes on disk are what was written; the same file named in other encodings. 'p' = a probe: no pass or fail, the nibbles are the result
    add("probe: ss (U+00DF), UTF-8 path (c3 9f)", "Stra\u00dfe.mp3", "p")
    add("probe: the same file, Latin-1 byte 0xDF", b"Stra\xdfe.mp3", "p")
    add("probe: the same file, code page 437/850 byte 0xE1", b"Stra\xe1e.mp3", "p")
    add("probe: o-slash (U+00F8), UTF-8 path (c3 b8)", "S\u00f8ren.mp3", "p")
    add("probe: the same file, Latin-1 byte 0xF8", b"S\xf8ren.mp3", "p")
    add("probe: the same file, code page 850 byte 0x9B", b"S\x9bren.mp3", "p")
    # round 3 (B-997): round 2 showed the Pocket decodes the path as CODE PAGE 437 (ss named with the byte 0xE1 opened). Do the letters with a decomposition (e-acute 0x82 ...) open against what macOS stored, and does 437 reach more than Latin letters?
    add("cp437: e-acute 0x82 against the NFC-written file of variant 1", b"nfc_Caf\x82.mp3", "p")
    add("cp437: e-acute 0x82 against the NFD-written file of variant 2", b"nfd_Caf\x82.mp3", "p")
    add("cp437: o-umlaut 0x94, U-umlaut 0x9A, i-umlaut 0x8B, n-tilde 0xA4, ss 0xE1 against variant 3", b"Bj\x94rk \x9An\x8Bcode \xa4 \xe1.mp3", "p")
    add("cp437: a FOLDER with e-acute 0x82 (variant 9's folder, stored NFD by macOS)", b"Caf\x82 Dir/a.mp3", "p")
    add("file: ae (U+00E6, no decomposition), UTF-8 path", "S\u00e6m.mp3", "p")
    add("cp437: ae named with 0x91", b"S\x91m.mp3", "p")
    add("file: cent sign (U+00A2; code page 437 0x9B, code page 850 has o-slash there), UTF-8 path", "Cost\u00a2.mp3", "p")
    add("cp437: the cent sign named with 0x9B (opens under 437, not under 850)", b"Cost\x9b.mp3", "p")
    add("file: Greek small pi (U+03C0; code page 437 0xE3), UTF-8 path", "Pi \u03c0.mp3", "p")
    add("cp437: pi named with 0xE3 (437 reaches more than Latin letters)", b"Pi \xe3.mp3", "p")
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
        if expect in "op" and vid not in FID: out[path[len(prefix):]] = mp3(vid)
    man = b"TNM2 %d\n" % len(vs) + b"".join(b"%02d|%s|%02d|%s\n" % (vid, expect.encode(), FID.get(vid, vid), path) for vid, expect, what, path in vs)
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


def clean_junk(dest):
    """macOS leaves AppleDouble files (`._<name>`) beside every file it writes to an exFAT or FAT card: they are not part of the matrix and the Pocket's library scan would meet them. Removes them under dest; returns how many"""
    import os
    n = 0
    for dp, dn, fn in os.walk(os.fsencode(str(dest))):
        for f in fn:
            if f.startswith(b"._"):
                try: os.remove(os.path.join(dp, f)); n += 1
                except FileNotFoundError: pass                              # macOS may have removed it itself meanwhile
    return n


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
    import os
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
        import time
        j = 0
        for _ in range(6):                                                  # macOS creates the ._ files lazily, after the write has been flushed: sweep until a sweep finds none
            os.sync(); time.sleep(1.0); k = clean_junk(dest); j += k
            if not k: break
        if j: print("removed %d macOS AppleDouble files (._*) the OS added beside the files" % j)
    res, listed = read_back(dest, fs)
    miss = [r for r, (ex, same) in res.items() if not ex]; diff = [r for r, (ex, same) in res.items() if ex and not same]
    print("read back: %d of %d files exist under the exact bytes written, %d differ in content" % (len(res) - len(miss), len(res), len(diff)))
    for r in miss: print("  NOT FOUND under the exact bytes (the OS refused or changed the name): %s  %s" % (osname(r), r.hex()))
    junk = [n for n in listed if os.path.basename(n).startswith(b"._")]
    print("the OS lists these entries (raw bytes, hex)%s:" % (" (%d AppleDouble ._* files left out: `write --yes` removes them)" % len(junk) if junk else ""))
    for n in listed:
        if n not in junk: print("  %s  %s" % (osname(n), n.hex()))
    base = {n.split(b"/")[-1] for n in listed}; changed = []
    for vid, expect, what, path in variants(root):
        for comp in path[len((root + DIR + "/").encode()):].split(b"/"):
            try: fd = unicodedata.normalize("NFD", comp.decode("utf-8")).encode()
            except UnicodeDecodeError: continue
            if fd != comp and comp not in base and fd in base: changed.append(vid)
    if changed: print("NOTE: the OS lists the precomposed (NFC) names of variants %s in their DECOMPOSED (NFD) form: this volume keeps accents decomposed (macOS on exFAT/FAT), so what is stored is NFD whatever was written" % sorted(set(changed)))
    return 1 if (miss or diff) else 0


if __name__ == "__main__":
    sys.exit(main())
