#!/usr/bin/env python3
"""Tau media library index builder, validator and reference reader (spec: docs/MEDIA_LIBRARY_0.4_SPEC.md).

  tau_library.py build  COMMON_DIR | --core ID [--card /Volumes/Pock] [--out FILE] [--cache FILE] [--playlists]
  tau_library.py verify INDEX [--root COMMON_DIR]      # parse, every invariant, optionally every path exists
  tau_library.py report INDEX
  tau_library.py synth  --tracks N --albums N --artists N --out FILE [--real-tracks COMMON_DIR] [--seed S]

The index describes what is ON THE CARD: build it from the destination Assets/tau/common folder (after
sync_media.py has made the names ASCII). Music files are only read, never modified. The output is written to a temp
file, re-parsed and verified, then renamed; the same input gives a byte-identical file (no timestamps).
Standard library only. The reader (`parse`) mirrors the firmware loader and returns the spec's E-codes.
"""
import argparse
import json
import os
import random
import re
import struct
import sys
import unicodedata
import zlib
from pathlib import Path

ROOT_PREFIX = "/Assets/tau/common/"
MAGIC = 0x42494C54          # 'TLIB'
VERSION = 1
HDR = 128
CAPS = dict(tracks=16384, albums=2048, artists=1024, playlists=64, file=4 << 20, strings=3 << 20)
MAX_PATH = 200
AUDIO = {".mp3": 1, ".flac": 2}
SEC = ("artists", "albums", "tracks", "strings", "album_by_title", "track_by_title", "letters", "playlists")
REC = {"artists": 8, "albums": 20, "tracks": 16, "playlists": 8}


class LibError(Exception):
    def __init__(self, code, msg):
        super().__init__(f"E{code}: {msg}")
        self.code = code


# ------------------------------------------------------------------ text
def ascii_text(s, limit=63):
    d = unicodedata.normalize("NFKD", s)
    d = "".join(c for c in d if not unicodedata.combining(c))
    d = "".join(c for c in d if 0x20 <= ord(c) < 0x7F)
    return " ".join(d.split())[:limit].strip()


def ascii_name(s):
    """Same conversion as sync_media.ascii_part (names already converted on the card pass through unchanged)."""
    d = unicodedata.normalize("NFKD", s)
    d = "".join(c for c in d if not unicodedata.combining(c))
    out = "".join(c for c in d if 0x20 <= ord(c) < 0x7F)
    for bad in '<>:"|?*\\':
        out = out.replace(bad, "_")
    return out.strip()


def sort_key(s):
    """(class, natural tokens): class 0 = non-letter first, 1..26 = A..Z; leading 'the '/'a ' ignored."""
    t = s.lower().strip()
    for art in ("the ", "a "):
        if t.startswith(art) and len(t) > len(art):
            t = t[len(art):]
            break
    cls = ord(t[0]) - 96 if t and "a" <= t[0] <= "z" else 0
    toks = tuple((0, int(x), "") if x.isdigit() else (1, 0, x) for x in re.split(r"(\d+)", t) if x)
    return (cls, toks)


def nat(s):
    return tuple((0, int(x), "") if x.isdigit() else (1, 0, x) for x in re.split(r"(\d+)", s.lower()) if x)


# ------------------------------------------------------------------ tags
def _dec(enc, b):
    try:
        if enc == 0:
            return b.decode("latin-1")
        if enc == 1:
            return b.decode("utf-16")
        if enc == 2:
            return b.decode("utf-16-be")
        return b.decode("utf-8")
    except UnicodeDecodeError:
        return b.decode("latin-1", "replace")


def _synchsafe(b):
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]


def read_id3v2(f):
    """-> (tags dict, total tag bytes incl. 10-byte header). Frames other than T* are skipped by seeking."""
    f.seek(0)
    h = f.read(10)
    if len(h) < 10 or h[:3] != b"ID3" or h[3] not in (3, 4):
        return {}, 0
    ver, flags, size = h[3], h[5], _synchsafe(h[6:10])
    end = 10 + size
    pos = 10
    if flags & 0x40:                                   # extended header
        eh = f.read(4)
        esz = _synchsafe(eh) if ver == 4 else struct.unpack(">I", eh)[0] + 4
        pos = 10 + (esz if ver == 4 else esz)
        f.seek(pos)
    tags = {}
    want = {"TIT2", "TPE1", "TPE2", "TALB", "TRCK", "TPOS", "TYER", "TDRC"}
    while pos + 10 <= end:
        f.seek(pos)
        fh = f.read(10)
        if len(fh) < 10 or fh[0] == 0:
            break
        fid = fh[:4].decode("latin-1")
        fsz = _synchsafe(fh[4:8]) if ver == 4 else struct.unpack(">I", fh[4:8])[0]
        if fsz <= 0 or pos + 10 + fsz > end:
            break
        if fid in want and fsz < 4096:
            body = f.read(fsz)
            if body:
                txt = _dec(body[0], body[1:]).split("\x00")[0]
                tags.setdefault(fid, txt.strip())
        pos += 10 + fsz
    return tags, end


def read_id3v1(f, size):
    if size < 128:
        return {}
    f.seek(size - 128)
    b = f.read(128)
    if b[:3] != b"TAG":
        return {}
    g = lambda a, z: b[a:z].split(b"\x00")[0].decode("latin-1").strip()
    t = {"TIT2": g(3, 33), "TPE1": g(33, 63), "TALB": g(63, 93), "TYER": g(93, 97)}
    if b[125] == 0 and b[126]:
        t["TRCK"] = str(b[126])
    return {k: v for k, v in t.items() if v}


_BR = {1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
       2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]}
_SR = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}


def mp3_seconds(f, start, size):
    """Xing/Info frame count, else CBR estimate from the first frame; 0 when unknown."""
    f.seek(start)
    buf = f.read(8192)
    for i in range(len(buf) - 4):
        if buf[i] == 0xFF and (buf[i + 1] & 0xE0) == 0xE0:
            b1, b2, b3 = buf[i + 1], buf[i + 2], buf[i + 3]
            ver, layer = (b1 >> 3) & 3, (b1 >> 1) & 3
            if ver == 1 or layer != 1 or (b2 >> 4) == 15 or (b2 >> 4) == 0 or ((b2 >> 2) & 3) == 3:
                continue
            fam = 1 if ver == 3 else 2
            br = _BR[fam][b2 >> 4] * 1000
            sr = _SR[ver][(b2 >> 2) & 3]
            spf = 1152 if ver == 3 else 576
            mono = (b3 >> 6) == 3
            side = (17 if mono else 32) if ver == 3 else (9 if mono else 17)
            x = i + 4 + side
            if buf[x:x + 4] in (b"Xing", b"Info") and x + 12 <= len(buf):
                if buf[x + 7] & 1:
                    frames = struct.unpack(">I", buf[x + 8:x + 12])[0]
                    return int(frames * spf / sr)
            if br:
                return int((size - start) * 8 / br)
            return 0
    return 0


def read_flac(f, size):
    f.seek(0)
    if f.read(4) != b"fLaC":
        return {}, 0
    tags, secs = {}, 0
    while True:
        h = f.read(4)
        if len(h) < 4:
            break
        last, typ, ln = h[0] >> 7, h[0] & 0x7F, int.from_bytes(h[1:4], "big")
        if typ == 0 and ln >= 18:
            b = f.read(ln)
            sr = (b[10] << 12) | (b[11] << 4) | (b[12] >> 4)
            total = ((b[13] & 15) << 32) | struct.unpack(">I", b[14:18])[0]
            secs = int(total / sr) if sr else 0
        elif typ == 4 and ln < 1 << 20:
            b = f.read(ln)
            p = 4 + struct.unpack("<I", b[:4])[0]
            n = struct.unpack("<I", b[p:p + 4])[0]
            p += 4
            for _ in range(n):
                cl = struct.unpack("<I", b[p:p + 4])[0]
                kv = b[p + 4:p + 4 + cl].decode("utf-8", "replace")
                p += 4 + cl
                if "=" in kv:
                    k, v = kv.split("=", 1)
                    m = {"TITLE": "TIT2", "ARTIST": "TPE1", "ALBUMARTIST": "TPE2", "ALBUM": "TALB",
                         "TRACKNUMBER": "TRCK", "DISCNUMBER": "TPOS", "DATE": "TYER"}.get(k.upper())
                    if m:
                        tags.setdefault(m, v.strip())
        else:
            f.seek(ln, 1)
        if last:
            break
    return tags, secs


def read_tags(path):
    size = path.stat().st_size
    with open(path, "rb") as f:
        if path.suffix.lower() == ".flac":
            tags, secs = read_flac(f, size)
            return tags, secs, 2
        tags, tag_end = read_id3v2(f)
        v1 = read_id3v1(f, size)
        for k, v in v1.items():
            tags.setdefault(k, v)
        secs = mp3_seconds(f, tag_end, size - (128 if v1 else 0))
        return tags, secs, 1


def _num(s, hi):
    m = re.match(r"\s*(\d+)", s or "")
    return min(int(m.group(1)), hi) if m else 0


def _year(t):
    m = re.search(r"(\d{4})", t.get("TDRC") or t.get("TYER") or "")
    return int(m.group(1)) if m else 0


# ------------------------------------------------------------------ scan
def scan(common, cache=None, playlists=False):
    """-> (entries, playlists, warnings). One entry per audio file, paths relative to `common` (POSIX)."""
    common = Path(common)
    warns, entries = [], []
    for p in sorted(common.rglob("*")):
        if not p.is_file() or p.name.startswith("._") or p.suffix.lower() not in AUDIO:
            continue
        rel = p.relative_to(common).as_posix()
        st = p.stat()
        key = f"{rel}|{st.st_size}|{st.st_mtime_ns}"
        if cache is not None and key in cache:
            tags, secs, fmt = cache[key]
        else:
            try:
                tags, secs, fmt = read_tags(p)
            except Exception as e:                               # unreadable tag: keep the file, use names
                warns.append(f"{rel}: tags unreadable ({e})")
                tags, secs, fmt = {}, 0, AUDIO[p.suffix.lower()]
            if cache is not None:
                cache[key] = [tags, secs, fmt]
        entries.append(dict(rel=rel, dir=rel.rpartition("/")[0], file=rel.rpartition("/")[2], tags=tags,
                            secs=secs, fmt=fmt))
    pls = []
    if playlists:
        by_rel = {e["rel"]: i for i, e in enumerate(entries)}
        used = set()
        for m in sorted(common.rglob("*.m3u")):
            if m.name.startswith("._"):
                continue
            base = m.parent.relative_to(common).as_posix()
            ids, dropped = [], 0
            for ln in m.read_text(encoding="utf-8", errors="replace").splitlines():
                ln = ln.strip()
                if not ln or ln.startswith("#"):
                    continue
                rel = ln.lstrip("/") if ln.startswith("/") else (f"{base}/{ln}" if base != "." else ln)
                if rel in by_rel:
                    ids.append(by_rel[rel])
                else:
                    dropped += 1
            if dropped:
                warns.append(f"{m.name}: {dropped} line(s) not in the library, dropped")
            # A list that is just the audio files of its own folder, in order, is an album list (sync_media generates
            # one per album folder): the library already has the album, so it is not imported as a playlist.
            own = [e["rel"] for e in entries if e["dir"] == (base if base != "." else "")]
            own.sort(key=lambda r: nat(r.rpartition("/")[2]))
            if base != "." and [entries[i]["rel"] for i in ids] == own:
                continue
            if not ids:
                continue
            if len(ids) > CAPS["tracks"]:
                warns.append(f"{m.name}: {len(ids)} entries, truncated to {CAPS['tracks']}")
                ids = ids[:CAPS["tracks"]]
            name = ascii_text(m.stem, 31)
            if not name or name.lower() == "playlist":          # the conventional file name: use the folder instead
                name = ascii_text(m.parent.name, 31) if base != "." else "Playlist"
            base_name, n = name or "Playlist", 2
            name = base_name
            while name.lower() in used:
                name = f"{base_name[:28]} {n}"; n += 1
            used.add(name.lower())
            pls.append(dict(name=name, rel_ids=ids))
    return entries, pls, warns


# ------------------------------------------------------------------ writer
class Pool:
    def __init__(self):
        self.buf = bytearray(b"\x00")          # offset 0 = empty string
        self.idx = {"": 0}

    def add(self, s):
        if s not in self.idx:
            self.idx[s] = len(self.buf)
            self.buf += s.encode("ascii") + b"\x00"
        return self.idx[s]


def _align(b, n=16):
    b += b"\x00" * (-len(b) % n)


def build_index(entries, pls=(), warns=None, root_prefix=ROOT_PREFIX):
    """entries -> index bytes. Raises LibError(14/15) for cap or path violations."""
    warns = warns if warns is not None else []
    albums = {}
    for i, e in enumerate(entries):
        t = e["tags"]
        title = ascii_text(t.get("TIT2", "")) or ascii_text(re.sub(r"^[\d\s._-]+", "", Path(e["file"]).stem)) or "Track"
        disc = _num(t.get("TPOS"), 63)
        trk = _num(t.get("TRCK"), 1023)
        e["_title"], e["_tno"] = title, (disc << 10) | trk
        albums.setdefault(e["dir"], []).append(i)
        if len((root_prefix + e["rel"]).encode()) > MAX_PATH:
            raise LibError(14, f"path over {MAX_PATH} bytes: {e['rel']}")
        if not (t.get("TIT2") and (t.get("TPE1") or t.get("TPE2"))):
            warns.append(f"{e['rel']}: missing title/artist tag, using names")
    alb = []
    for d, ids in albums.items():
        ids.sort(key=lambda i: (entries[i]["_tno"] >> 10, entries[i]["_tno"] & 1023, nat(entries[i]["file"])))
        first = entries[ids[0]]["tags"]
        artist = "Unknown Artist"
        for i in ids:
            a = ascii_text(entries[i]["tags"].get("TPE2", "")) or ascii_text(entries[i]["tags"].get("TPE1", ""))
            if a:
                artist = a
                break
        title = ascii_text(first.get("TALB", "")) or ascii_text(d.rpartition("/")[2]) or "Unknown Album"
        year = next((y for y in (_year(entries[i]["tags"]) for i in ids) if y), 0)
        alb.append(dict(dir=d, ids=ids, artist=artist, title=title, year=year))
    art_names = sorted({a["artist"] for a in alb}, key=lambda s: (sort_key(s), s))
    art_idx = {n: i for i, n in enumerate(art_names)}
    alb.sort(key=lambda a: (art_idx[a["artist"]], a["year"], sort_key(a["title"]), a["dir"]))
    n_tr, n_al, n_ar = len(entries), len(alb), len(art_names)
    if n_tr > CAPS["tracks"] or n_al > CAPS["albums"] or n_ar > CAPS["artists"] or len(pls) > CAPS["playlists"]:
        raise LibError(14, f"library too large: {n_tr} tracks, {n_al} albums, {n_ar} artists, {len(pls)} playlists")
    pool = Pool()
    root = pool.add(root_prefix)
    tracks, track_new = bytearray(), {}
    albums_b = bytearray()
    first_alb = {}
    for ai, a in enumerate(alb):
        first_alb.setdefault(art_idx[a["artist"]], [ai, 0])[1] += 1
        albums_b += struct.pack("<IIHHHHHH", pool.add(a["title"]), pool.add(a["dir"]), art_idx[a["artist"]],
                                a["year"], len(tracks) // 16, len(a["ids"]), 0xFFFF, 0)
        for i in a["ids"]:
            e = entries[i]
            track_new[i] = len(tracks) // 16
            tracks += struct.pack("<IIHHHBB", pool.add(e["_title"]), pool.add(e["file"]), min(e["secs"], 65535),
                                  e["_tno"], ai, e["fmt"], 0)
    artists_b = b"".join(struct.pack("<IHH", pool.add(n), *first_alb[i]) for i, n in enumerate(art_names))
    album_titles = [a["title"] for a in alb]
    track_titles = [entries[i]["_title"] for a in alb for i in a["ids"]]
    alb_order = sorted(range(n_al), key=lambda i: (sort_key(album_titles[i]), i))
    trk_order = sorted(range(n_tr), key=lambda i: (sort_key(track_titles[i]), i))

    def letters(names):
        cls = [sort_key(n)[0] for n in names]
        out = []
        for c in range(27):
            out.append(next((p for p, k in enumerate(cls) if k >= c), len(cls)))
        return out
    lt = letters(art_names) + letters([album_titles[i] for i in alb_order]) + letters([track_titles[i] for i in trk_order])
    pl_b, items = bytearray(), bytearray()
    for p in pls:
        ids = [track_new[i] for i in p["rel_ids"]]
        pl_b += struct.pack("<IHH", pool.add(p["name"]), len(items) // 2, len(ids))
        items += b"".join(struct.pack("<H", i) for i in ids)
    body = {
        "artists": bytes(artists_b), "albums": bytes(albums_b), "tracks": bytes(tracks), "strings": bytes(pool.buf),
        "album_by_title": b"".join(struct.pack("<H", i) for i in alb_order),
        "track_by_title": b"".join(struct.pack("<H", i) for i in trk_order),
        "letters": b"".join(struct.pack("<H", v) for v in lt),
        "playlists": bytes(pl_b) + bytes(items),
    }
    out = bytearray(HDR)
    table = []
    for name in SEC:
        _align(out)
        table.append((len(out), len(body[name])))
        out += body[name]
    _align(out)
    if len(out) > CAPS["file"] or len(pool.buf) > CAPS["strings"]:
        raise LibError(14, "index over the size caps")
    crc = zlib.crc32(bytes(out[HDR:])) & 0xFFFFFFFF
    flags = 2 if pls else 0
    struct.pack_into("<IHHIIIIII", out, 0, MAGIC, VERSION, VERSION, HDR, flags, crc, len(out), crc, 0)
    struct.pack_into("<HHHHII", out, 32, n_ar, n_al, n_tr, len(pls), root, 0)
    for k, (o, ln) in enumerate(table):
        struct.pack_into("<II", out, 48 + 8 * k, o, ln)
    struct.pack_into("<I", out, 124, zlib.crc32(bytes(out[:124])) & 0xFFFFFFFF)
    return bytes(out)


# ------------------------------------------------------------------ reader / validator
class Index:
    pass


def parse(data, sample_walk=True):
    """Reference loader: same checks and E-codes as the firmware (spec section 6)."""
    if len(data) < HDR:
        raise LibError(11, "shorter than the header")
    m, ver, minv, hs, flags, bid, fsz, bcrc, _r = struct.unpack_from("<IHHIIIIII", data, 0)
    if m != MAGIC or minv > VERSION or hs != HDR:
        raise LibError(11, "bad magic / version / header size")
    if zlib.crc32(data[:124]) & 0xFFFFFFFF != struct.unpack_from("<I", data, 124)[0]:
        raise LibError(11, "header CRC")
    if fsz != len(data):
        raise LibError(12, f"size {len(data)} != header {fsz}")
    if zlib.crc32(data[HDR:]) & 0xFFFFFFFF != bcrc:
        raise LibError(13, "body CRC")
    n_ar, n_al, n_tr, n_pl, root, art_id = struct.unpack_from("<HHHHII", data, 32)
    if n_tr > CAPS["tracks"] or n_al > CAPS["albums"] or n_ar > CAPS["artists"] or n_pl > CAPS["playlists"] \
            or fsz > CAPS["file"]:
        raise LibError(14, "count above the caps")
    ix = Index()
    ix.data, ix.flags, ix.build_id, ix.art_id = data, flags, bid, art_id
    ix.n = dict(artists=n_ar, albums=n_al, tracks=n_tr, playlists=n_pl)
    ix.sec = {}
    for k, name in enumerate(SEC):
        o, ln = struct.unpack_from("<II", data, 48 + 8 * k)
        if o % 16 or o < HDR or o + ln > len(data):
            raise LibError(15, f"section {name} out of range")
        ix.sec[name] = (o, ln)
    want = dict(artists=n_ar * 8, albums=n_al * 20, tracks=n_tr * 16, album_by_title=n_al * 2,
                track_by_title=n_tr * 2, letters=27 * 3 * 2)
    for name, ln in want.items():
        if ix.sec[name][1] != ln:
            raise LibError(15, f"section {name} length {ix.sec[name][1]} != {ln}")
    so, sl = ix.sec["strings"]
    ix.strings = data[so:so + sl]
    if root >= sl:
        raise LibError(17, "root string out of range")
    ix.root = ix.str(root)

    def rec(name, size, fmt):
        o, ln = ix.sec[name]
        return [struct.unpack_from(fmt, data, o + i * size) for i in range(ln // size)]
    ix.artists = rec("artists", 8, "<IHH")
    ix.albums = rec("albums", 20, "<IIHHHHHH")
    ix.tracks = rec("tracks", 16, "<IIHHHBB")
    lo, ll = ix.sec["playlists"]
    if ll < 8 * n_pl or (ll - 8 * n_pl) & 1:
        raise LibError(15, "playlists section length")
    n_items = (ll - 8 * n_pl) // 2
    ix.lists = [struct.unpack_from("<IHH", data, lo + 8 * i) for i in range(n_pl)]
    ix.items = list(struct.unpack_from(f"<{n_items}H", data, lo + 8 * n_pl)) if n_items else []
    for i, (nm, first, cnt) in enumerate(ix.lists):
        if nm >= sl or first + cnt > n_items or cnt > CAPS["tracks"]:
            raise LibError(17, f"playlist {i} out of range")
    if sample_walk:
        for i in list(range(min(256, n_tr))) + list(range(256, n_tr, 64)):
            t = ix.tracks[i]
            if t[0] >= sl or t[1] >= sl or t[4] >= n_al:
                raise LibError(17, f"track {i} out of range")
        for i in range(n_al):
            a = ix.albums[i]
            if a[0] >= sl or a[1] >= sl or a[2] >= n_ar or a[4] + a[5] > n_tr:
                raise LibError(17, f"album {i} out of range")
        for i in list(range(min(256, len(ix.items)))) + list(range(256, len(ix.items), 64)):
            if ix.items[i] >= n_tr:
                raise LibError(17, f"playlist item {i} out of range")
    return ix


def _str(ix, off):
    if off >= len(ix.strings):
        raise LibError(17, f"string offset {off}")
    end = ix.strings.index(b"\x00", off)
    return ix.strings[off:end].decode("ascii")


Index.str = _str


def track_path(ix, i):
    t = ix.tracks[i]
    d = ix.str(ix.albums[t[4]][1])
    return ix.root + (d + "/" if d else "") + ix.str(t[1])


def verify(data, root=None):
    """-> list of problem strings (empty = valid). Checks every invariant of spec section 2."""
    bad = []
    try:
        ix = parse(data)
    except LibError as e:
        return [str(e)]
    n = ix.n
    pos, prev_artist = 0, -1
    for ai, a in enumerate(ix.albums):
        if a[4] != pos:
            bad.append(f"album {ai}: first_track {a[4]} != running {pos}")
        pos += a[5]
        if a[5] == 0:
            bad.append(f"album {ai}: no tracks")
        if a[2] < prev_artist:
            bad.append(f"album {ai}: artist order")
        prev_artist = a[2]
    if pos != n["tracks"]:
        bad.append(f"album track counts sum {pos} != {n['tracks']}")
    cover = 0
    for i, ar in enumerate(ix.artists):
        if ar[1] != cover:
            bad.append(f"artist {i}: first_album {ar[1]} != {cover}")
        cover += ar[2]
        for k in range(ar[1], ar[1] + ar[2]):
            if k >= n["albums"] or ix.albums[k][2] != i:
                bad.append(f"artist {i}: album {k} not its own")
                break
    if cover != n["albums"]:
        bad.append(f"artist album counts sum {cover} != {n['albums']}")
    for i in range(1, len(ix.artists)):
        if sort_key(ix.str(ix.artists[i][0])) < sort_key(ix.str(ix.artists[i - 1][0])):
            bad.append(f"artist {i}: not sorted")
            break
    for ai, a in enumerate(ix.albums):
        prev = None
        for i in range(a[4], a[4] + a[5]):
            t = ix.tracks[i]
            k = (t[3] >> 10, t[3] & 1023, nat(ix.str(t[1])))
            if t[4] != ai:
                bad.append(f"track {i}: album {t[4]} != {ai}")
            if prev is not None and k < prev:
                bad.append(f"track {i}: play order")
            prev = k
    for i in range(n["tracks"]):
        p = track_path(ix, i)
        if len(p) > MAX_PATH:
            bad.append(f"track {i}: path {len(p)} bytes")
        if root is not None and not (Path(root) / p[len(ix.root):]).is_file():
            bad.append(f"track {i}: file missing {p}")
    for name, cnt, view in (("album_by_title", n["albums"], lambda j: ix.str(ix.albums[j][0])),
                            ("track_by_title", n["tracks"], lambda j: ix.str(ix.tracks[j][0]))):
        o, ln = ix.sec[name]
        order = list(struct.unpack_from(f"<{cnt}H", data, o)) if cnt else []
        if sorted(order) != list(range(cnt)):
            bad.append(f"{name}: not a permutation")
            continue
        keys = [(sort_key(view(j)), j) for j in order]
        if keys != sorted(keys):
            bad.append(f"{name}: not sorted")
    lo, _ = ix.sec["letters"]
    lt = struct.unpack_from("<81H", data, lo)
    for v, (cnt) in enumerate([n["artists"]] * 27 + [n["albums"]] * 27 + [n["tracks"]] * 27):
        if lt[v] > cnt or (v % 27 and lt[v] < lt[v - 1]):
            bad.append(f"letters[{v}] invalid")
            break
    for i, (nm, first, cnt) in enumerate(ix.lists):
        if cnt == 0:
            bad.append(f"playlist {i}: empty")
        if any(t >= n["tracks"] for t in ix.items[first:first + cnt]):
            bad.append(f"playlist {i}: track id out of range")
    for name in ("strings",):
        for s in ix.strings.split(b"\x00"):
            if any(c < 0x20 or c >= 0x7F for c in s):
                bad.append("non-ASCII string in the pool")
                break
    return bad


def report(data):
    ix = parse(data)
    lens = [len(track_path(ix, i)) for i in range(ix.n["tracks"])]
    secs = {k: v[1] for k, v in ix.sec.items()}
    print(f"tracks {ix.n['tracks']}  albums {ix.n['albums']}  artists {ix.n['artists']}  playlists {ix.n['playlists']}")
    print(f"file {len(data):,} B ({len(data) / 1048576:.2f} MiB)  build_id {ix.build_id:08X}  root {ix.root}")
    print("sections: " + "  ".join(f"{k} {v:,}" for k, v in secs.items()))
    if lens:
        print(f"path length: max {max(lens)}  mean {sum(lens) / len(lens):.0f}  (limit {MAX_PATH})")
    unk = sum(1 for t in ix.tracks if t[2] == 0)
    nt = sum(1 for t in ix.tracks if (t[3] & 1023) == 0)
    print(f"unknown duration {unk}  unknown track number (tile shows 00) {nt}")


# ------------------------------------------------------------------ synthetic
_W = ("Amber Blue Cold Dawn Echo Fire Glass Hollow Iron Jade Kite Lunar Moss Neon Opal Pale Quill Rust Silk Tide "
      "Umber Velvet Wild Xenon Yarn Zinc River Stone Night Ghost Paper Copper Ember Harbor Meadow Signal Static").split()


def synth(tracks, albums, artists, seed=1, real=None):
    rnd = random.Random(seed)
    entries = list(real or [])
    names = [" ".join(rnd.choice(_W) for _ in range(rnd.randint(1, 2))) + f" {i}" for i in range(artists)]
    per = [tracks // albums + (1 if i < tracks % albums else 0) for i in range(albums)]
    left = tracks - len(entries)
    for a in range(albums):
        if left <= 0:
            break
        art = names[a % artists]
        alb = " ".join(rnd.choice(_W) for _ in range(rnd.randint(2, 4)))
        d = f"Synthetic/{ascii_text(art, 24)}/{ascii_text(alb, 28)} {a}"
        for t in range(per[a]):
            if left <= 0:
                break
            title = " ".join(rnd.choice(_W) for _ in range(rnd.randint(2, 5)))
            entries.append(dict(rel=f"{d}/{t + 1:02d} {title}.mp3", dir=d, file=f"{t + 1:02d} {title}.mp3",
                                tags=dict(TIT2=title, TPE1=art, TPE2=art, TALB=alb, TRCK=f"{t + 1}/{per[a]}",
                                          TYER=str(1970 + a % 50)), secs=rnd.randint(90, 480), fmt=1))
            left -= 1
    return entries


# ------------------------------------------------------------------ CLI
def write_verified(data, out, root=None):
    bad = verify(data, root)
    if bad:
        for b in bad[:20]:
            print("  PROBLEM " + b, file=sys.stderr)
        raise SystemExit(f"index failed verification ({len(bad)} problem(s)); nothing written")
    out = Path(out)
    tmp = out.with_name(out.name + ".tmp")
    tmp.write_bytes(data)
    if tmp.read_bytes() != data:
        raise SystemExit("write verification failed")
    os.replace(tmp, out)


def build_dir(common, out=None, cache_path=None, playlists=False, quiet=False):
    common = Path(common)
    cache = {}
    if cache_path and Path(cache_path).exists():
        cache = {k: v for k, v in json.loads(Path(cache_path).read_text()).items()}
    entries, pls, warns = scan(common, cache if cache_path else None, playlists)
    if not entries:
        raise SystemExit(f"no .mp3/.flac files under {common}")
    # The index holds ABSOLUTE card paths, so the root is the platform folder the music lives in:
    # .../Assets/<platform>/common -> /Assets/<platform>/common/  (anything else keeps the default).
    root_prefix = ROOT_PREFIX
    if common.resolve().name == "common" and common.resolve().parent.parent.name == "Assets":
        root_prefix = f"/Assets/{common.resolve().parent.name}/common/"
    data = build_index(entries, pls, warns, root_prefix)
    out = Path(out) if out else common / "tau-library.tdb"
    write_verified(data, out, root=common)
    if cache_path:
        Path(cache_path).write_text(json.dumps(cache))
    if not quiet:
        for w in warns[:15]:
            print("  warning:", w)
        if len(warns) > 15:
            print(f"  ... {len(warns) - 15} more warnings")
        print(f"wrote {out} ({len(data):,} B)")
        report(data)
    return data


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("common", nargs="?")
    b.add_argument("--core")
    b.add_argument("--card", default="/Volumes/Pock")
    b.add_argument("--out")
    b.add_argument("--cache")
    b.add_argument("--playlists", action="store_true")
    v = sub.add_parser("verify")
    v.add_argument("index")
    v.add_argument("--root")
    r = sub.add_parser("report")
    r.add_argument("index")
    s = sub.add_parser("synth")
    s.add_argument("--tracks", type=int, default=7180)
    s.add_argument("--albums", type=int, default=800)
    s.add_argument("--artists", type=int, default=300)
    s.add_argument("--seed", type=int, default=1)
    s.add_argument("--out", required=True)
    s.add_argument("--real-tracks")
    a = ap.parse_args(argv)
    if a.cmd == "build":
        common = a.common
        if a.core:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            import sync_media
            common = Path(a.card) / "Assets" / sync_media.core_platform(Path(a.card), a.core) / "common"
        if not common:
            ap.error("give COMMON_DIR or --core")
        build_dir(common, a.out, a.cache, a.playlists)
    elif a.cmd == "verify":
        bad = verify(Path(a.index).read_bytes(), a.root)
        for x in bad[:30]:
            print("PROBLEM", x)
        print("OK" if not bad else f"{len(bad)} problem(s)")
        return 1 if bad else 0
    elif a.cmd == "report":
        report(Path(a.index).read_bytes())
    elif a.cmd == "synth":
        real = scan(a.real_tracks)[0] if a.real_tracks else None
        data = build_index(synth(a.tracks, a.albums, a.artists, a.seed, real))
        write_verified(data, a.out)
        print(f"wrote {a.out}")
        report(data)
    return 0


if __name__ == "__main__":
    sys.exit(main())
