#!/usr/bin/env python3
"""Builds the NAME TEST LIBRARY: one MP3 per case of the shared name corpus (sim/name_corpus.py, 62 cases; the 7 a file system cannot create or keep apart are left out: NFC/NFD and case twins, over-long names, a dot file),
named with the ORIGINAL file name and tagged in UTF-8 (title, artist, album from the case; the album is the case's category). It is the source tree for `tools/sync_media.py ... --library`: the card gets the converted names, the index,
and the name sidecar (docs/features/NAME_SIDECAR_SPEC.md), and the Pocket's library lists show what the rules do with accents, Cyrillic, CJK, emoji, collisions and edge names. Used for BUG-001 on `TAU DEV 217` to `219` (audit B-1005 to B-1007)
and as the shared reproduction for Tau Omega (message 009).

  python3 tools/make_name_test_library.py --out /tmp/Name\\ Test\\ Library                      silent audio, 30 s per file
  python3 tools/make_name_test_library.py --out DIR --audio some.mp3                          that MP3's audio (its own tags dropped) in every file: real sound, full length
  python3 tools/make_name_test_library.py --out DIR --audio some.mp3 --seconds 10             only the first 10 s of it (about 128 kbit/s worth)
  then: python3 tools/sync_media.py DIR --card /Volumes/Pock --core alfatreze.TAU --library --no-art-variants      (--dry-run first; a card write needs the owner's go)
"""
import argparse, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "sim"))
import name_corpus as C
import name_sidecar as NS

SKIP = {"nfd_cafe", "norm_b", "case_b", "long_ascii", "long_cjk", "long_cp437", "dotfile"}      # what a normal file system cannot create or keep apart
SILENT_FRAME = b"\xff\xfb\x90\x64" + b"\x00" * 413                                              # one MPEG-1 layer III frame, 128 kbit/s 44.1 kHz, about 26 ms of silence


def ss(n):
    return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])


def frame(fid, text):
    b = b"\x03" + text.encode("utf-8")
    return fid.encode() + ss(len(b)) + b"\x00\x00" + b


def audio_of(path):
    """the MPEG frames of an MP3: everything after its ID3v2 tag, from the first frame sync"""
    b = Path(path).read_bytes(); i = 0
    if b[:3] == b"ID3":
        i = 10 + ((b[6] << 21) | (b[7] << 14) | (b[8] << 7) | b[9])
    while i + 1 < len(b) and not (b[i] == 0xFF and b[i + 1] & 0xE0 == 0xE0):
        i += 1
    if i + 1 >= len(b):
        raise SystemExit(f"{path}: no MPEG audio found")
    return b[i:]


def build(out, audio=None, seconds=None):
    out = Path(out)
    if audio:
        body = audio_of(audio)
        if seconds: body = body[:int(16000 * seconds)]                 # 128 kbit/s = 16,000 bytes a second
    else:
        body = SILENT_FRAME * int((seconds or 30) * 38.28)             # 38.28 frames a second at 44.1 kHz
    made = []
    for c in C.CASES:
        if c["id"] in SKIP:
            continue
        tg = c.get("tags") or (None, None, None)
        title = NS.clean_text(tg[0]) or NS.clean_text(c["name"]) or "x"
        artist = NS.clean_text(tg[1]) or "Artist " + c["cat"]
        album = NS.clean_text(tg[2]) or c["cat"]
        tag = frame("TIT2", title) + frame("TPE1", artist) + frame("TALB", album)
        p = out / c["cat"] / (c["name"] + ".mp3")
        try:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"ID3" + bytes([4, 0, 0]) + ss(len(tag)) + tag + body)
        except OSError as e:
            print("skipped", c["id"], e, file=sys.stderr); continue
        made.append(p)
    return made


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True, type=Path); ap.add_argument("--audio", type=Path); ap.add_argument("--seconds", type=float)
    a = ap.parse_args()
    if a.out.exists() and any(a.out.iterdir()): sys.exit(f"refused: {a.out} is not empty")
    m = build(a.out, a.audio, a.seconds)
    print(f"{len(m)} files in {a.out} ({sum(p.stat().st_size for p in m) / 1e6:.1f} MB)")
