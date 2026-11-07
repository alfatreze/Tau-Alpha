#!/usr/bin/env python3
"""Copy music, playlists and images onto the Pocket SD card for one or more Tau cores.

Every numbered test build (TAU PSRAM NN) needs its own copy of the media, because a core reads
Assets/<platform>/common/. This does that in one step and is meant to grow into the media library sync:
it takes files or folders, converts images the player can use, copies to each destination core,
verifies every file by SHA-256 and can write a manifest.

  sync_media.py "Nausicaa OST" playlist.m3u --core alfatreze.TAU_PSRAM_03 --core alfatreze.TAU_PSRAM_04
  sync_media.py MUSIC_DIR --all-tau                      # every alfatreze.TAU* core on the card
  sync_media.py MUSIC_DIR --from-core alfatreze.TAU --core alfatreze.TAU_PSRAM_05   # clone one core's media
  add --library to build and verify the media library index (tau-library.tdb) in each destination after copying (with it the name sidecar tau-names.json and
  tau-names.bin, docs/features/NAME_SIDECAR_SPEC.md: the original file names and tags where they differ from what the index can show; --no-names skips it),
  add --dry-run to see the plan, --mirror to delete files in the destination folder that are not in the source,
  --manifest FILE to write what was done as JSON.

Rules:
  * Folders keep their name under common/ ("Nausicaa OST" -> common/Nausicaa OST); loose files go to common/.
  * Audio (.mp3 .flac) and playlists (.m3u .m3u8) are copied unchanged. MP3 cover art is checked with
    tools/library_check.py (what the core would do with it) and reported; tags are never rewritten.
  * Standalone images are made player-safe: the decoder reads baseline JPEG only, so PNG/other formats and
    progressive JPEGs are converted to baseline JPEG (macOS sips), long side limited to --max-image, and the result
    must fit the firmware size cap. Already-fine JPEGs are copied byte for byte.
  * The player reads cover art ONLY from inside the track (MP3 APIC / FLAC PICTURE), never from a cover.jpg beside it.
    --embed-cover therefore writes the folder's cover image (cover.jpg, folder.jpg, front.jpg or cover-*.jpg, or
    --cover FILE) into the COPIES of that folder's tracks (replacing any existing art); the source files are never
    changed. The cover is first made player-safe (baseline JPEG, <= --max-image px, under the 2 MiB firmware cap).
    Loose image files are not copied unless --copy-images (the player would not use them).
  * A folder of tracks with no .m3u/.m3u8 gets a playlist.m3u generated in the destination (bare filenames, natural
    track order) for the media library to import as a "list"; --no-playlist turns that off.
  * Names keep every character the Pocket can open (BUG-001, docs/issues/001): it reads a path as code page 437, so Nausicaä, Café, Søren's
    ö/é/ñ/ß/æ stay as they are (written precomposed); only a character outside code page 437 (Cyrillic, CJK, emoji, Á, Ø, Č ...) is
    transliterated (Japanese with pykakasi if installed, the rest with anyascii; emoji dropped). Folders and files both, and every playlist line is rewritten to match. --ascii-only folds ALL
    non-ASCII characters (the old behaviour); --keep-names leaves names alone. Two names that convert to the same name get " (2)", " (3)" in sorted order.
  * Cover sidecars are written BY DEFAULT (B-736): with no flag the run behaves as --art-variants (pal256, 128 px); --no-art-variants turns it off. The firmware reads
    them (fw/timg.inc) and uses them in preference to decoding the embedded JPEG, which is slow and may be removed. Missing Pillow/numpy only warns when the default is
    in force. --art-variants [LIST] writes pre-scaled cover files in the study formats of tools/tau_image.py (default
    pal256 at 128 px on the long side, scaled proportionally, never cropped or padded; 'auto' picks per cover; see
    docs/IMAGE_FORMATS.md) to <album>/tau-art/cover_<size>.<variant>.timg,
    from the cover: --cover, else a named cover image in the folder (cover.jpg, folder.jpg, front.jpg, cover-*), else art embedded in the first track that has
    any (MP3 APIC or FLAC PICTURE), else any image in the folder or one level below (a front/cover name first, then the largest). They are
    sidecars, copied unchanged on later --from-core clones. Needs Pillow and numpy.
  * Files that are not media are skipped (listed); ._* and .DS_Store are never copied.
Exit status 1 if any verification fails or a requested core is not found.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
try:
    import library_check as lib
except Exception:            # the checker is an aid; the sync works without it
    lib = None

AUDIO = {".mp3", ".flac"}
LISTS = {".m3u", ".m3u8"}
SIDECAR = {".timg"}          # art variants written by --art-variants; carried across unchanged
IMAGES = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tif", ".tiff", ".heic", ".webp"}
SKIP_NAMES = {".DS_Store", "Thumbs.db"}
ART_MAX_BYTES = getattr(lib, "ART_MAX_BYTES", 2 * 1024 * 1024) if lib else 2 * 1024 * 1024


def sha(p, chunk=1 << 20):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def is_junk(p: Path):
    return p.name.startswith("._") or p.name in SKIP_NAMES


def core_platform(card: Path, core: str):
    j = card / "Cores" / core / "core.json"
    if not j.exists():
        return None
    return json.loads(j.read_text())["core"]["metadata"]["platform_ids"][0]


def find_cores(card: Path, args):
    names = list(args.core or [])
    if args.all_tau:
        names += sorted(p.name for p in (card / "Cores").iterdir()
                        if p.is_dir() and p.name.startswith("alfatreze.TAU"))
    out, seen = [], set()
    for n in names:
        if n in seen:
            continue
        seen.add(n)
        plat = core_platform(card, n)
        if plat is None:
            print(f"ERROR: core {n!r} not found on the card", file=sys.stderr)
            return None
        out.append((n, plat))
    return out


def gather(sources):
    """-> list of (source file Path, relative destination path under common/)."""
    items = []
    for s in sources:
        s = Path(s)
        if s.is_dir():
            for p in sorted(s.rglob("*")):
                if p.is_file() and not is_junk(p) and not any(x.startswith("._") for x in p.parts):
                    items.append((p, Path(s.name) / p.relative_to(s)))
        elif s.is_file() and not is_junk(s):
            items.append((s, Path(s.name)))
        else:
            print(f"warning: {s} not found, skipped", file=sys.stderr)
    return items


def jpeg_info(p):
    """(width, height, progressive) or None."""
    if lib is None:
        return None
    return lib.jpeg_dims(open(p, "rb").read(1 << 20))


def prepare_image(src: Path, rel: Path, tmp: Path, max_px, quality, force=False):
    """-> (file to copy, relative destination, note). Converts when the player could not use it."""
    ext = src.suffix.lower()
    info = jpeg_info(src) if ext in (".jpg", ".jpeg") else None
    ok_jpeg = info and not info[2] and max(info[0], info[1]) <= max_px and src.stat().st_size <= ART_MAX_BYTES
    if ok_jpeg and not force:
        return src, rel, "baseline JPEG, copied as is"
    out = tmp / (rel.stem + ".jpg")
    n = 0
    while out.exists():
        n += 1
        out = tmp / f"{rel.stem}_{n}.jpg"
    cmd = ["sips", "-s", "format", "jpeg", "-s", "formatOptions", str(quality), "-Z", str(max_px), str(src), "--out", str(out)]
    if not shutil.which("sips"):
        return None, rel, "needs conversion but sips is not available (macOS only)"
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or not out.exists():
        return None, rel, "conversion failed: " + (r.stderr or r.stdout).strip()[:120]
    q = quality
    while out.stat().st_size > ART_MAX_BYTES and q > 40:      # stay under the firmware cap
        q -= 10
        subprocess.run(cmd[:5] + [str(q)] + cmd[6:], capture_output=True)
    after = jpeg_info(out)
    if after and after[2]:
        return None, rel, "conversion still progressive"
    return out, rel.with_suffix(".jpg"), (f"converted to baseline JPEG q{q} (was {ext or 'no ext'}, "
                                          f"{src.stat().st_size:,} -> {out.stat().st_size:,} bytes)")


def ascii_part(name):
    import unicodedata
    d = unicodedata.normalize("NFKD", name)
    d = "".join(c for c in d if not unicodedata.combining(c))
    out = "".join(c for c in d if 0x20 <= ord(c) < 0x7F)      # accents -> plain letter, no equivalent -> removed
    for bad in '<>:"|?*\\':
        out = out.replace(bad, "_")
    out = out.strip()
    stem, dot, ext = out.rpartition(".")
    if not out or (dot and not stem):
        out = "track" + (("." + ext) if dot else "")             # a name that was entirely non-ASCII
    return out


def _translit(run):
    """a run of characters the Pocket cannot open, as plain ASCII. Japanese kana and kanji go through pykakasi (Hepburn, words split by a space)
    when it is installed, everything else through anyascii (Cyrillic, Greek, Hanzi as pinyin, ...); without them an accent is dropped to the
    plain letter and the rest is removed. Both libraries are OPTIONAL and never bundled (pykakasi is GPL-3.0: an installed tool, nothing copied;
    docs/PROVENANCE.md). Emoji and symbols are dropped."""
    import unicodedata
    out = []
    for c in run:
        o = ord(c)
        if o >= 0x1F000 or 0x2190 <= o <= 0x2BFF or 0xFE00 <= o <= 0xFE0F or o == 0x200D:
            continue                                               # emoji, arrows, dingbats, variation selectors, joiner
        out.append(c)
    run = "".join(out)
    if not run:
        return ""
    kana = any(0x3040 <= ord(c) <= 0x30FF or 0x31F0 <= ord(c) <= 0x31FF for c in run)
    if kana:                                                       # a run with kana is Japanese: kanji get their Japanese reading, not pinyin
        k = _kakasi()
        if k:
            run = " ".join(w["hepburn"] for w in k.convert(run) if w["hepburn"])
    if _anyascii():
        run = _anyascii()(run)
    else:
        run = "".join(unicodedata.normalize("NFKD", _FOLD.get(c, c)) for c in run)
    return "".join(x for x in run if 0x20 <= ord(x) < 0x7F)


_KAKASI = None
_ANY = None


def _kakasi():
    global _KAKASI
    if _KAKASI is None:
        try:
            import pykakasi
            _KAKASI = pykakasi.kakasi()
        except Exception:
            _KAKASI = False
    return _KAKASI or None


def _anyascii():
    global _ANY
    if _ANY is None:
        try:
            from anyascii import anyascii
            _ANY = anyascii
        except Exception:
            _ANY = False
    return _ANY or None


_RESERVED = {"con", "prn", "aux", "nul"} | {f"com{i}" for i in range(1, 10)} | {f"lpt{i}" for i in range(1, 10)}
ROOT_BUDGET = 30            # "/Assets/tau_psram_07/common/" is the longest root prefix; the index refuses a whole path over 200 bytes
MAX_PATH = 200
FOLDER_MAX = 64


def _is_greek(c):
    return 0x370 <= ord(c) <= 0x3FF or 0x1F00 <= ord(c) <= 0x1FFF


def card_part(name, ascii_only=False, is_file=True):
    """a file or folder name as it goes on the card (rules: docs/features/CARD_NAME_RULES.md). The Pocket decodes a path as code page 437
    (docs/issues/001, B-1000), so every character that exists there (accented Latin letters, ss, ae, cent, ...) is KEPT in the precomposed
    (NFC) form the card stores; a run of other characters is transliterated (_translit). A run of Greek letters is kept only when ALL of it
    is in code page 437 (pi, mu), else the whole run is transliterated, so a Greek word never comes out half Greek, half Latin.
    Controls become a space; dropped characters leave no double space; the stem loses leading/trailing spaces and dots; a Windows device
    name (CON, NUL, COM1 ...) gets a trailing underscore; an empty name is "track" (the caller numbers duplicates).
    With ascii_only every non-ASCII character is folded (the old behaviour, --ascii-only)."""
    import re
    import unicodedata
    if ascii_only:
        return ascii_part(name)
    chars = [" " if (ord(c) < 0x20 or 0x7F <= ord(c) <= 0x9F) else c for c in unicodedata.normalize("NFC", name)]
    keep = [0x20 <= ord(c) < 0x7F or (ord(c) > 0x9F and _cp437(c)) for c in chars]
    k = 0
    while k < len(chars):                                         # Greek: all of a run or none of it
        if _is_greek(chars[k]):
            e = k
            while e < len(chars) and _is_greek(chars[e]):
                e += 1
            if not all(keep[k:e]):
                keep[k:e] = [False] * (e - k)
            k = e
        else:
            k += 1
    out, run = [], []
    for c, kp in zip(chars, keep):
        if kp:
            if run:
                out.append(_translit("".join(run)) or "\0"); run = []
            out.append(c)                                         # ASCII, or in code page 437: the Pocket opens it
        else:
            run.append(c)
    if run:
        out.append(_translit("".join(run)) or "\0")
    s2 = re.sub(r" ?\0 ?", lambda m: " " if " " in m.group(0) else "", "".join(out)).replace("\0", "")
    for bad in '<>:"|?*\\':
        s2 = s2.replace(bad, "_")
    stem, dot, ext = s2.rpartition(".")
    if not (is_file and dot and 1 <= len(ext) <= 5 and " " not in ext):
        stem, ext = s2, ""
        dot = ""
    stem = stem.strip(" .")
    if not stem:
        stem = "track"
    if stem.lower() in _RESERVED:
        stem += "_"
    return stem + (("." + ext) if dot else "")


class NameMap:
    """original card-relative path -> converted path, every folder and file; two different originals that convert to the same name (compared
    without case: the card is ExFAT) in one folder get " (2)", " (3)" ... in sorted order, so a run never stops on a collision and the result
    is the same on every run (the same rule is in docs/features/CARD_NAME_RULES.md for Tau Omega)."""

    def __init__(self, rels, ascii_only=False):
        self.ascii_only = ascii_only
        self.map = {(): ()}
        self.taken = {}
        for rel in sorted({self._nfc(r.parts) for r in rels}):
            for i in range(1, len(rel) + 1):
                self._one(rel[:i], last_is_file=(i == len(rel)))

    @staticmethod
    def _nfc(parts):
        import unicodedata
        return tuple(unicodedata.normalize("NFC", x) for x in parts)

    def _one(self, orig, last_is_file):
        if orig in self.map:
            return
        parent = self.map[orig[:-1]]
        base = card_part(orig[-1], self.ascii_only, last_is_file)
        stem, ext = base, ""
        if last_is_file and "." in base and base.rpartition(".")[0]:
            stem, _, e = base.rpartition("."); ext = "." + e
        if last_is_file:                                              # keep the whole path under the index's limit, room kept for " (99)"
            room = MAX_PATH - ROOT_BUDGET - len("/".join(parent)) - (1 if parent else 0) - len(ext) - 5
        else:
            room = FOLDER_MAX
        if len(stem) > max(room, 8):
            stem = stem[:max(room, 8)].rstrip(" .")
            base = stem + ext
        n, cand = 1, base
        while True:
            key = (parent, cand.lower())
            if self.taken.get(key, orig) == orig:
                break
            n += 1
            cand = f"{stem} ({n}){ext}"
        self.taken[(parent, cand.lower())] = orig
        self.map[orig] = parent + (cand,)

    def rel(self, rel):
        return Path(*self.map[self._nfc(Path(rel).parts)])

    def line(self, base_parts, parts):
        """a playlist line (relative to the playlist's folder), converted with the same map; pieces the map does not know use card_part"""
        for pre in (self._nfc(base_parts), ()):
            k = pre + self._nfc(parts)
            if k in self.map:
                return "/".join(self.map[k][len(self.map[pre]):])
        return "/".join(card_part(x, self.ascii_only, i == len(parts) - 1) for i, x in enumerate(parts))


_FOLD = {"Ø": "O", "ø": "o", "Đ": "D", "đ": "d", "Ł": "L", "ł": "l", "Ð": "D", "ð": "d", "Þ": "Th", "þ": "th", "Œ": "OE", "œ": "oe", "ı": "i"}


def _cp437(c):
    try: c.encode("cp437"); return True
    except UnicodeEncodeError: return False




def natural_key(name):
    import re
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", name.lower())]


def _syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def _unsyncsafe(b):
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]


def embed_mp3(src: Path, jpeg: bytes, out: Path):
    """Write jpeg as the only APIC of a copy of src. Returns None or a reason it could not."""
    data = src.read_bytes()
    kept, audio, major = [], data, 3
    if data[:3] == b"ID3":
        major, flags = data[3], data[5]
        if major not in (3, 4):
            return f"ID3v2.{major} not supported"
        if flags & 0xD0:
            return "ID3 unsynchronisation / extended header / footer not supported"
        size = _unsyncsafe(data[6:10])
        body, audio = data[10:10 + size], data[10 + size:]
        p = 0
        while p + 10 <= len(body):
            fid = body[p:p + 4]
            if fid[0] == 0:
                break                                    # padding
            fs = _unsyncsafe(body[p + 4:p + 8]) if major == 4 else int.from_bytes(body[p + 4:p + 8], "big")
            if fs <= 0 or p + 10 + fs > len(body):
                return "malformed ID3 frame"
            if fid != b"APIC":
                kept.append(body[p:p + 10 + fs])
            p += 10 + fs
    pic = b"\x00" + b"image/jpeg\x00" + b"\x03" + b"\x00" + jpeg
    fsz = _syncsafe(len(pic)) if major == 4 else len(pic).to_bytes(4, "big")
    kept.append(b"APIC" + fsz + b"\x00\x00" + pic)
    tag = b"".join(kept)
    out.write_bytes(b"ID3" + bytes([major, 0, 0]) + _syncsafe(len(tag)) + tag + audio)
    return None


def embed_flac(src: Path, jpeg: bytes, wh, out: Path):
    data = src.read_bytes()
    if data[:4] != b"fLaC":
        return "not a FLAC file"
    p, blocks = 4, []
    while True:
        h = data[p]
        typ, ln = h & 0x7F, int.from_bytes(data[p + 1:p + 4], "big")
        blocks.append((typ, data[p + 4:p + 4 + ln]))
        p += 4 + ln
        if h & 0x80:
            break
    audio = data[p:]
    blocks = [b for b in blocks if b[0] not in (1, 6)]          # drop padding and old pictures
    mime = b"image/jpeg"
    pic = (3).to_bytes(4, "big") + len(mime).to_bytes(4, "big") + mime + (0).to_bytes(4, "big") \
        + wh[0].to_bytes(4, "big") + wh[1].to_bytes(4, "big") + (24).to_bytes(4, "big") + (0).to_bytes(4, "big") \
        + len(jpeg).to_bytes(4, "big") + jpeg
    blocks.append((6, pic))
    o = bytearray(b"fLaC")
    for i, (typ, body) in enumerate(blocks):
        o += bytes([(0x80 if i == len(blocks) - 1 else 0) | typ]) + len(body).to_bytes(3, "big") + body
    out.write_bytes(bytes(o) + audio)
    return None


COVER_NAMES = ("cover.jpg", "cover.jpeg", "folder.jpg", "front.jpg", "cover.png", "folder.png")


def find_cover(folder: Path):
    for n in COVER_NAMES:
        if (folder / n).is_file():
            return folder / n
    g = sorted(p for p in folder.glob("cover-*") if p.suffix.lower() in IMAGES and not is_junk(p))
    return g[0] if g else None


PREFERRED_STEMS = ("front", "cover", "case front", "folder", "album", "booklet front")


def find_any_image(folder: Path):
    """Last-resort cover: an image in the folder or one level below (Artwork/, Scans/ ...). A name that says front/cover wins;
    otherwise the largest file (a booklet page or scan is rarely bigger than the real cover art). None if there is no image."""
    cands = [p for p in folder.iterdir() if p.is_file() and p.suffix.lower() in IMAGES and not is_junk(p)]
    for sub in sorted(d for d in folder.iterdir() if d.is_dir() and not is_junk(d)):
        cands += [p for p in sub.iterdir() if p.is_file() and p.suffix.lower() in IMAGES and not is_junk(p)]
    if not cands:
        return None
    for stem in PREFERRED_STEMS:
        hit = sorted((p for p in cands if stem in p.stem.lower()), key=lambda p: (p.parent != folder, -p.stat().st_size, p.name))
        if hit:
            return hit[0]
    return sorted(cands, key=lambda p: (-p.stat().st_size, str(p)))[0]


def art_variants(folders_parent, info, args, tmp, start):
    """Encode the study formats for one album folder. -> list of plan entries (file, rel, note, converted)."""
    try:
        import tau_image as ti
        from PIL import Image
    except ImportError as e:
        if getattr(args, "art_implicit", False):
            if not getattr(args, "art_warned", False):
                args.art_warned = True
                print(f"WARNING: cover sidecars skipped (Pillow/numpy missing: {e}); pip3 install pillow numpy, or pass --no-art-variants to silence this")
            return []
        sys.exit(f"--art-variants needs Pillow and numpy ({e}); pip3 install pillow numpy")
    import io
    names = ti.DEFAULT_VARIANTS if args.art_variants == "default" else args.art_variants
    variants = [v for v in names.split(",") if v]
    for v in variants:
        if v != "auto" and v not in ti.VARIANTS:
            sys.exit(f"unknown art variant {v!r}; known: auto, {', '.join(ti.VARIANTS)}")
    img, what = None, None
    cover = Path(args.cover) if args.cover else find_cover(info["dir"])
    try:
        if cover and cover.is_file():
            img, what = Image.open(cover), cover.name
        else:
            # 1. art embedded in a track (MP3 APIC or FLAC PICTURE): the first track that has one
            for a in sorted(info["audio"], key=natural_key):
                raw = ti.extract_embedded(info["dir"] / a)
                if raw:
                    img, what = Image.open(io.BytesIO(raw)), f"art embedded in {a}"
                    break
            # 2. any image in the folder or one level below
            if img is None:
                any_img = find_any_image(info["dir"])
                if any_img:
                    img, what = Image.open(any_img), f"{any_img.relative_to(info['dir'])} (no named cover, no embedded art)"
    except Exception as e:
        print(f"warning: art variants for {folders_parent}: cover unreadable ({e})", file=sys.stderr)
        return []
    if img is None:
        print(f"note: no cover found for {folders_parent}; no art variants", file=sys.stderr)
        return []
    out = []
    for size in args.art_size or [ti.DEFAULT_SIZE]:
        rgb = ti.fit_long_side(img, size)
        for v in variants:
            data, used, ps = (ti.encode_auto(rgb) if v == "auto" else (None, v, None))
            if data is None:
                data = ti.encode(rgb, v)
                ps = ti.score(rgb, data)
            lo, hi = ti.estimate_ms(data)
            f = tmp / f"art_{start + len(out)}.timg"
            f.write_bytes(data)
            leaf = f"cover_{size}.timg" if v == "auto" else f"cover_{size}.{v}.timg"
            note = f"{what} -> {size}px {used}, {len(data):,} B, {ps:.1f} dB, load est {lo:.0f}-{hi:.0f} ms"
            out.append((f, folders_parent / "tau-art" / leaf, note, True))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="*", help="files or folders to copy")
    ap.add_argument("--card", default=os.environ.get("TAU_CARD", "/Volumes/CARDWRITE"), help="card root (default: $TAU_CARD, else /Volumes/CARDWRITE)")
    ap.add_argument("--core", action="append", help="destination core id, e.g. alfatreze.TAU_PSRAM_03 (repeatable)")
    ap.add_argument("--all-tau", action="store_true", help="every alfatreze.TAU* core on the card")
    ap.add_argument("--from-core", help="also use this core's Assets/<platform>/common media as a source")
    ap.add_argument("--mirror", action="store_true", help="delete files in the destination folders that are not in the source")
    ap.add_argument("--dry-run", action="store_true", help="show the plan, write nothing")
    ap.add_argument("--max-image", type=int, default=2500, help="longest cover side in pixels (default 2500; the firmware handles up to about 2560)")
    ap.add_argument("--quality", type=int, default=90, help="JPEG quality for converted images (default 90)")
    ap.add_argument("--embed-cover", action="store_true", help="write each folder's cover image into the copies of its tracks")
    ap.add_argument("--cover", help="use this image as the cover for every track (implies --embed-cover)")
    ap.add_argument("--copy-images", action="store_true", help="also copy loose image files (converted if needed)")
    ap.add_argument("--no-playlist", action="store_true", help="do not generate playlist.m3u for folders that have none")
    ap.add_argument("--keep-names", action="store_true", help="do not convert names at all (a name with a character outside code page 437 cannot be opened by the player)")
    ap.add_argument("--no-names", action="store_true", help="with --library: do not write the name sidecar (tau-names.json / tau-names.bin, the original names and tags beside the card-safe ones)")
    ap.add_argument("--ascii-only", action="store_true", help="fold every non-ASCII character in names (the old behaviour); default keeps the code page 437 characters the Pocket can open")
    ap.add_argument("--cover-quality", type=int, help="re-encode embedded covers at this JPEG quality (e.g. 75); default: keep the original bytes")
    ap.add_argument("--cover-max", type=int, help="shrink embedded covers to at most this many pixels on the long side (the screen shows 92 px)")
    ap.add_argument("--dest-suffix", default="", help="append this text to every copied top-level folder name (keep two variants side by side)")
    ap.add_argument("--art-variants", nargs="?", const="default", metavar="LIST",
                    help="also write pre-scaled cover variants (comma list of pal256,pal64,pal16,bc1,jpg60,jpg75,jpg85,rgb565,auto; "
                         "no value = pal256) into <album>/tau-art/")
    ap.add_argument("--no-art-variants", action="store_true", help="do not write cover sidecars (they are written by default)")
    ap.add_argument("--art-size", type=int, action="append", help="long-side size in px for --art-variants (repeatable, default 128; aspect ratio kept)")
    ap.add_argument("--manifest", help="write a JSON record of the run to this file")
    ap.add_argument("--library", action="store_true", help="after copying, build and verify tau-library.tdb in each destination (tools/tau_library.py)")
    args = ap.parse_args()
    args.art_implicit = False
    if args.no_art_variants:
        args.art_variants = None
    elif args.art_variants is None:
        args.art_variants = "default"; args.art_implicit = True      # B-736: sidecars are the default

    card = Path(args.card)
    if not (card / "Cores").is_dir():
        sys.exit(f"{card} does not look like a Pocket card (no Cores folder); is it mounted?")
    cores = find_cores(card, args)
    if cores is None:
        return 1
    if not cores:
        sys.exit("no destination: give --core ID (repeatable) or --all-tau")

    sources = [Path(s) for s in args.sources]
    if args.from_core:
        plat = core_platform(card, args.from_core)
        if plat is None:
            sys.exit(f"--from-core {args.from_core!r} not found on the card")
        common = card / "Assets" / plat / "common"
        for p in sorted(common.iterdir()):
            if not is_junk(p) and p.name not in ("tau.rom", "tau-loading.bin"):
                sources.append(p)
    if not sources:
        sys.exit("nothing to copy: give files or folders, or --from-core")

    items = gather(sources)
    if args.dest_suffix:
        items = [(sp, Path(rl.parts[0] + args.dest_suffix, *rl.parts[1:]) if len(rl.parts) > 1 else rl) for sp, rl in items]
    top_map = {}
    if args.dest_suffix:
        for (sp, rl), (_, rl0) in zip(items, gather(sources)):
            if len(rl.parts) > 1: top_map[rl0.parts[0]] = rl.parts[0]
    skipped, plan = [], []
    tmp = Path(tempfile.mkdtemp(prefix="sync_media_"))
    try:
        # per source folder: has a playlist? which cover?
        folders = {}
        for src, rel in items:
            if len(rel.parts) < 2:
                continue
            f = folders.setdefault(rel.parent, {"dir": src.parent, "audio": [], "list": False})
            if src.suffix.lower() in AUDIO:
                f["audio"].append(src.name)
            elif src.suffix.lower() in LISTS and src.parent == f["dir"]:
                f["list"] = True
        cover_cache = {}

        def cover_for(rel_parent):
            if args.cover:
                key = "override"
            else:
                key = rel_parent
            if key not in cover_cache:
                c = Path(args.cover) if args.cover else find_cover(folders[rel_parent]["dir"]) if rel_parent in folders else None
                if c is None or not c.is_file():
                    cover_cache[key] = None
                else:
                    f2, _, note = prepare_image(c, Path(c.name), tmp, min(args.max_image, args.cover_max or args.max_image),
                                                args.cover_quality or args.quality, force=bool(args.cover_quality or args.cover_max))
                    if f2 is None:
                        print(f"warning: cover {c} unusable: {note}", file=sys.stderr)
                        cover_cache[key] = None
                    else:
                        wh = jpeg_info(f2)
                        cover_cache[key] = (c, f2.read_bytes(), wh, note)
            return cover_cache[key]

        embed = args.embed_cover or bool(args.cover)
        img_skipped = 0
        for src, rel in items:
            ext = src.suffix.lower()
            if ext in AUDIO or ext in LISTS:
                note, use = "", src
                if ext in AUDIO and embed:
                    cv = cover_for(rel.parent)
                    if cv is None:
                        note = "no cover image found; tracks unchanged"
                    else:
                        c, jpg, wh, cnote = cv
                        n = 0
                        out = tmp / f"emb_{len(plan)}{ext}"
                        why = embed_mp3(src, jpg, out) if ext == ".mp3" else embed_flac(src, jpg, wh, out)
                        if why:
                            note = f"cover NOT embedded ({why})"
                        else:
                            use = out
                            note = f"cover embedded: {c.name} {wh[0]}x{wh[1]}" if wh else f"cover embedded: {c.name}"
                if ext == ".mp3" and lib:
                    try:
                        v, why, _ = lib.verdict(str(use))
                        note = (note + "; " if note else "") + f"art: {v}" + ("" if v == "ok" else f" ({why})")
                    except Exception as e:
                        note = (note + "; " if note else "") + f"art check failed: {e}"
                elif ext == ".flac" and use is src:
                    note = (note + "; " if note else "") + "art: not checked"
                plan.append((use, rel, note, use is not src))
            elif ext in SIDECAR:
                plan.append((src, rel, "art variant, copied as is", False))
            elif ext in IMAGES:
                if not args.copy_images:
                    img_skipped += 1
                    continue
                f, r2, note = prepare_image(src, rel, tmp, args.max_image, args.quality)
                if f is None:
                    skipped.append((src, note))
                else:
                    plan.append((f, r2, note, f != src))
            else:
                skipped.append((src, "not a supported media type"))
        if args.art_variants:
            for rp, info in sorted(folders.items(), key=lambda kv: str(kv[0])):
                if info["audio"]:
                    have = {str(r) for _, r, _, _ in plan}          # a --from-core clone already carries its sidecars
                    plan.extend(e for e in art_variants(rp, info, args, tmp, len(plan)) if str(e[1]) not in have)
        if not args.no_playlist:
            for rp, info in sorted(folders.items(), key=lambda kv: str(kv[0])):
                if info["audio"] and not info["list"]:
                    pl = tmp / f"pl_{len(plan)}.m3u"
                    pl.write_text("\n".join(sorted(info["audio"], key=natural_key)) + "\n", encoding="utf-8")
                    plan.append((pl, rp / "playlist.m3u", f"generated, {len(info['audio'])} tracks", True))

        origin_map = {}                                  # converted card path -> original path, for the name sidecar (tau-names.json / .bin)
        if not args.keep_names:
            renamed, newplan = [], []
            nm = NameMap([rel for _, rel, _, _ in plan], args.ascii_only)
            for f, rel, note, conv in plan:
                r2 = nm.rel(rel)
                if rel.suffix.lower() in AUDIO:
                    origin_map[r2.as_posix()] = rel.as_posix()
                if r2 != rel:
                    renamed.append((rel, r2))
                if rel.suffix.lower() in LISTS:            # playlist lines follow the file names
                    lines, changed = [], False
                    for ln in f.read_text(encoding="utf-8", errors="replace").splitlines():
                        if not ln.strip() or ln.lstrip().startswith("#"):
                            n = ln
                        else:
                            parts = ln.split("/")
                            if len(parts) > 1 and parts[0] in top_map:
                                parts[0] = top_map[parts[0]]
                            n = nm.line(rel.parent.parts, parts)
                        changed |= n != ln
                        lines.append(n)
                    if changed:
                        pl2 = tmp / f"plr_{len(newplan)}.m3u"
                        pl2.write_text("\n".join(lines) + "\n", encoding="utf-8")
                        f, conv = pl2, True
                        note = (note + "; " if note else "") + "playlist lines converted"
                newplan.append((f, r2, note, conv))
            plan = newplan
            if renamed:
                folders_r = sorted({(a.parent, b.parent) for a, b in renamed if a.parent != b.parent})
                print(f"names converted: {len(renamed)} file(s)" +
                      "".join(f"\n  {a}  ->  {b}" for a, b in folders_r[:6]))
        record, bad = [], 0
        for core, plat in cores:
            base = card / "Assets" / plat / "common"
            print(f"\n== {core}  ->  Assets/{plat}/common/")
            wanted = set()
            for f, rel, note, conv in plan:
                dest = base / rel
                wanted.add(dest.resolve())
                sh = sha(f)
                state = "new"
                if dest.exists():
                    state = "same" if (dest.stat().st_size == f.stat().st_size and sha(dest) == sh) else "update"
                line = f"  {state:6} {rel}" + (f"   [{note}]" if note else "")
                print(line)
                rec = {"core": core, "dest": str(rel), "sha256": sh, "bytes": f.stat().st_size,
                       "state": state, "converted": conv, "note": note}
                if state != "same" and not args.dry_run:
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(f, dest)
                    if sha(dest) != sh:
                        print(f"  VERIFY FAILED {rel}", file=sys.stderr)
                        bad += 1
                        rec["state"] = "verify-failed"
                record.append(rec)
            if args.mirror:
                # only inside the folders this run copies into; loose files in common/ are never touched
                for top in sorted({rel.parts[0] for _, rel, _, _ in plan if len(rel.parts) > 1}):
                    for p in sorted((base / top).rglob("*")) if (base / top).exists() else []:
                        if p.is_file() and not is_junk(p) and p.resolve() not in wanted:
                            print(f"  delete {p.relative_to(base)}")
                            if not args.dry_run:
                                p.unlink()
            if not args.dry_run:
                for p in list(base.rglob("._*")):    # macOS metadata we may have created
                    try:
                        p.unlink()
                    except FileNotFoundError:
                        pass
            if args.library and not args.dry_run:
                import tau_library
                print(f"  building library index for {core} ...")
                try:
                    tau_library.build_dir(base, playlists=True, origins=origin_map, names=not args.no_names)
                except (SystemExit, tau_library.LibError) as e:
                    print(f"  LIBRARY INDEX FAILED: {e}", file=sys.stderr)
                    bad += 1
        if img_skipped:
            print(f"\n{img_skipped} loose image file(s) not copied (the player only reads art inside the track; "
                  f"use --embed-cover, or --copy-images)")
        if skipped:
            print("\nskipped:")
            for s, why in skipped:
                print(f"  {s}  ({why})")
        if args.manifest:
            Path(args.manifest).write_text(json.dumps({"cores": [c for c, _ in cores], "files": record,
                                                       "skipped": [[str(s), w] for s, w in skipped]}, indent=2) + "\n")
        n = sum(1 for r in record if r["state"] in ("new", "update"))
        print(f"\n{'plan' if args.dry_run else 'done'}: {len(plan)} file(s) x {len(cores)} core(s), "
              f"{n} to write/written, {sum(1 for r in record if r['state'] == 'same')} already identical"
              + (f", {bad} VERIFY FAILURES" if bad else ""))
        if not args.dry_run:
            os.sync() if hasattr(os, "sync") else None
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
